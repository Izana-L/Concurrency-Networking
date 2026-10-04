
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <HttpServer.hpp>
#include <iostream>
#include <thread>
using std::cout;
using std::endl;

namespace argb
{

   
    HttpRequestHandler::Ptr HttpServer::RequestHandlerManager::create_handler
    (
        HttpRequest::Method method,
        std::string_view    request_path
    )
    const
    {
        for (auto * factory : handler_factories)
        {
            if (auto handler = factory->create_handler (method, request_path))
            {
                return handler;
            }
        }

        return nullptr;
    }

    void HttpServer::run (const Address & address, const Port & port)
    {
        listener.listen (address, port);
        {
            ListenerScopeGuard guard{ listener };

            running = true;
            /// Se utiliza funciones lamda para compactar el codigo en una linea y como forma de familiarizar con ellas.
            /// Utilizando std::bind funcionaría igual pero se tendria que declarar antes el stoken.
            thread_directory.connections_manager_thread = std::jthread([this](std::stop_token stoken) {accept_and_close_inactive_connections(stoken); });
            thread_directory.worker_thread = std::jthread([this](std::stop_token stoken) {concurrent_transfer_data(stoken); });

            run_handlers();
           
        }
    }
    /// <summary>
    /// Esta funcon se ha creado para encapsular los funciones que debe de hacer el hilo connections_manager_thread.
    /// Ya que cerrar conexiones no exige una inmediatez se hace cada dos segundos para que el servidor no este cerrando conexiones 
    /// todo el rato consumiendo demás. 
    /// </summary>
    /// <param name="stoken"></param>
    void HttpServer::accept_and_close_inactive_connections(std::stop_token stoken)
    {
       
        using namespace std::literals;
        using namespace std::chrono;
        thread_directory.wait_rest_threads();

        constexpr auto cleanup_interval = 2s;

        auto next_cleanup = steady_clock::now() + cleanup_interval;

        while (!stoken.stop_requested())
        {           
            accept_connections();
            
            if (steady_clock::now() >= next_cleanup)
            {
                close_inactive_connections();
                next_cleanup = steady_clock::now() + cleanup_interval;
            }

            std::this_thread::sleep_for(10ms);
        }
    }
    void HttpServer::accept_connections()
    {
        try
        {
            while (auto new_socket = listener.accept())
            {
                //primero se obtiene el Handle y acontinuacion se crea un shared ptr 
                //y se prepara la estructura que se va a trabajar en este caso con ConnectionContext
                TcpSocket::Handle socket_handle = new_socket->get_handle();

                auto context = std::make_shared<ConnectionContext>();
                context->socket = std::move(*new_socket);
                context->socket.set_blocking(false);
                ///Después se encolan a  connections_vector para que la funcion de close_inactive_connections las cierre en un futuro.
                ///Se añaden a new_connections_queue y se notifica para que el hilo que se encarga de concurrent_transfer_data se entere 
                ///que hay nuevas conexiones que procesar
                connections_vector.push_back(context);
                new_connections_queue.push(context);
                transfer_cv.notify_one();
            }
        }
        catch (const NetworkException& exception)
        {
            cout << "Error accepting new connection: " << exception << endl;
        }
    }
    void HttpServer::close_inactive_connections()
    {
        /// primero se obtiene el tiempo actual
        const auto current_time = now();

        /// He fusionado la logica de ordenacion de connections en connections_vector con el cierre de conexiones 
        auto new_end = std::remove_if(connections_vector.begin(), connections_vector.end(),[&](const std::shared_ptr<ConnectionContext>& context)
        {
            /// Se comprueba si esta siendo procesado para que no se cierre ninguna conexion que este siendo procesado
            /// no pasa nada que una conexion deba esparar al siguente ciclo cerrar conexiones.
            /// Esta comprobacion está por si acaso, no tengo claro si puede ocurrir que se cierre una conexion siendo procesada 
            /// por otro hilo.
                

            if (context->state == ConnectionContext::CLOSED)
            {
                ConnectionContext::LifecycleState expected = ConnectionContext::LifecycleState::IDLE;
                if (context->lifecycle.compare_exchange_strong(expected, ConnectionContext::LifecycleState::CLOSING))
                {
                    context->socket.close();
                    context->lifecycle.store(ConnectionContext::LifecycleState::CLOSED);
                    return true;
                }
                
            }
            /// Encaso de que no este en el estado CLOSED pero su timeout haya acabado se cierra igualmente por eso 
            ///se comprueba que no este procesandose ya que puede ocurrir que no pase por el state CLOSED pero cerrarse aun así.
            if (current_time - context->last_activity.load() > connection_timeout)
            {
                std::cout << "Closing connection " << context->socket.get_handle() << " due to timeout." << std::endl;
                context->state = ConnectionContext::CLOSED;
                ConnectionContext::LifecycleState expected = ConnectionContext::LifecycleState::IDLE;
                if (context->lifecycle.compare_exchange_strong(expected, ConnectionContext::LifecycleState::CLOSING))
                {
                    context->socket.close();
                    context->lifecycle.store(ConnectionContext::LifecycleState::CLOSED);
                    return true;
                }
            }

            return false;
        });
        // Se borran las conexiones que ya esten cerradas
        connections_vector.erase(new_end, connections_vector.end());

    }
    /// <summary>
    /// Esta función ejecuta el bucle principal de transferencia de datos para el servidor HTTP: espera nuevas conexiones o 
    /// señales de escritura, procesa una a una las conexiones activas moviendo sus datos y, si quedan en un estado apropiado, 
    /// las encamina a una cola de manejadores. También retira las conexiones que ya están cerradas.
    /// 
    /// Tiene la mayor peso de concurrencia ya que esta funcion comparte todos los contenedores que tiene y es el puente entre hilos
    /// </summary>
    /// <param name="stoken"></param>
    void HttpServer::concurrent_transfer_data(std::stop_token stoken)
    {
        // espera a que todos los hilos esten listo para la ejecución igual que todos los hilos
        thread_directory.wait_rest_threads();

        // este es el vector de conexiones que solo será utilizado en esta funcion por ello no necesita ningun sistema concurrente
        // 
        std::vector<std::shared_ptr<ConnectionContext>> active_connections;

        while (!stoken.stop_requested()) 
        {       
            /// este hilo espará a que haya una notificacion si se ha anñadido algun elemento a estas queue o espera 10ms para 
            /// que compruebe si hay algo que procesar en caso negativo simplemente espera haciendo más liviana la ejcucion del server
            {
                std::unique_lock lock(transfer_mutex);
                transfer_cv.wait_for(lock, std::chrono::milliseconds(10), [&]
                    {
                        return !new_connections_queue.empty() ||
                               !pending_writes_count.isCero();
                    });
                if (!running) break;
            }

            //Se añaden todas las nuevas conexiones aceptadas en el hilo aceptador
            while (auto optional_context = new_connections_queue.try_pop())
            {
                active_connections.push_back(std::move(*optional_context));
            }
            pending_writes_count.reset();


            // Se recorren todas las conexiones
            for (auto& context : active_connections) 
            {
                // Se comprueba si no se han cerrado por si acaso y que no tengan el estado CLOSED
                //desde que se llamo a la función
                ConnectionContext::LifecycleState expected = ConnectionContext::LifecycleState::IDLE;
                if (context->lifecycle.compare_exchange_strong(expected, ConnectionContext::LifecycleState::PROCESSING))
                {
                    if (context->state != ConnectionContext::CLOSED)
                    {
               
                        transfer_data_by_one(*context);
                        
                        context->lifecycle.store(ConnectionContext::LifecycleState::IDLE);
                        /// En caso de que el estado de la conexion sea RUNNING_HANDLER se pasa a ready_handlers_queue para que 
                        /// en run_handlers se procese
                        auto pushable_state = ConnectionContext::RUNNING_HANDLER;
                        if (context->state.compare_exchange_strong(pushable_state, ConnectionContext::HANDLER_IN_PROGRESS))
                        {
                            ready_handlers_queue.push(context);
                        }
                    }
                    
                }    
            }

            /// se borrran las conexiones que se han cerrado
            active_connections.erase( std::remove_if(active_connections.begin(), active_connections.end(),[](const auto& context) 
                {
                    return context->state == ConnectionContext::CLOSED || context->lifecycle == ConnectionContext::LifecycleState::CLOSED;
                }),
            active_connections.end());
        }
    }
    /// <summary>
    /// La funcion transfer_data la he modificdo quitando el for que tenia el resto de función sigue igual  
    /// </summary>
    /// <param name="socket_handle"></param>
    /// <param name="context"></param>
    void HttpServer::transfer_data_by_one(ConnectionContext& context)
    {
        try
        {
            switch (context.state)
            {
                case ConnectionContext::RECEIVING_REQUEST:       receive_request(context);       break;
                case ConnectionContext::WRITING_RESPONSE_HEADER: write_response_header(context); break;
                case ConnectionContext::WRITING_RESPONSE_BODY:   write_response_body(context);   break;
            }
        }
        catch (const NetworkException& exception) 
        {
            std::cout << "Error during data transfer on connection " << context.socket.get_handle() << ": " << exception << std::endl;
            context.state = ConnectionContext::CLOSED;
        }
    }

    void HttpServer::receive_request (ConnectionContext & context)
    {
        IoBuffer buffer;
        size_t   received = context.socket.receive (buffer);

        if (received == TcpSocket::receive_closed) 
        {
            context.state = ConnectionContext::CLOSED;
        } 
        else
        if (received != TcpSocket::receive_empty) 
        {
            bool parsed = context.request_parser.parse ({ buffer.data (), received });

            if (parsed) 
            {
                context.handler = request_handler_manager.create_handler (context.request.get_method (), context.request.get_path ());

                if (context.handler)
                {
                    context.state = ConnectionContext::RUNNING_HANDLER;
                }
                else
                {
                    static constexpr std::string_view not_found_message = "File not found";

                    HttpResponse::Serializer(context.response)
                        .status     (404)
                        .header     ("Content-Type",   "text/plain; charset=utf-8")
                        .header     ("Content-Length", std::to_string (not_found_message.size ()))
                        .header     ("Connection",     "close")
                        .end_header ()
                        .body       (not_found_message);

                    context.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                }
            }

            context.last_activity = now ();
        }
    }

    void HttpServer::write_response_header (ConnectionContext & context)
    {
        auto   header    = std::as_bytes  (context.response.get_serialized_header ());
        auto   remaining = header.subspan (context.response_bytes_sent);

        size_t sent = context.socket.send (remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now ();
        }

        if (context.response_bytes_sent == header.size ())
        {
            context.state = ConnectionContext::WRITING_RESPONSE_BODY;
            context.response_bytes_sent = 0;
        }
    }

    void HttpServer::write_response_body (ConnectionContext & context)
    {
        auto body = std::as_bytes (context.response.get_body ());

        if (body.empty ())
        {
            context.state = ConnectionContext::CLOSED;
            return;
        }

        auto remaining = body.subspan (context.response_bytes_sent);
        
        size_t sent = context.socket.send (remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now ();
        }

        if (context.response_bytes_sent == body.size ())
        {
            context.state = ConnectionContext::CLOSED;
        }
    }
    /// <summary>
    /// Esta función se encarga de procesar las conexiones que ya están listas para ejecutar su manejador: 
    /// extrae los contextos de la cola de manejadores pendientes y, según el tipo de manejador, los encamina 
    /// a la cola de tareas Lua o directamente al pool de hilos para completar su procesamiento.
    /// </summary>
    void HttpServer::run_handlers ()
    {
        thread_directory.wait_rest_threads();

        while (running)
        {
            /// se extrae la conexion 
            auto maybe_context = ready_handlers_queue.pop();  

            if (!maybe_context) break; 

            std::shared_ptr<ConnectionContext> context = std::move(*maybe_context);

            if (context->handler)
            {
                /// Aqui se comprueba si es un handle de lua o nativo 
                /// en metodo getHandlerType es un metodo que he implentado para saber 
                /// que tipo HandlerType es cada handle sin saber su especicion real.
                if (context->handler->getHandlerType() == HandlerType::LUA )
                {
                    thread_directory.lua_task_queue.push([this, context](){ handler_process(context);});
                }
                else
                {
                    post(thread_directory.thread_pool, [this, context](){ handler_process(context);});
                }    
            }
        }

    }
    /// <summary>
    ///  La logica de esta funcion la he separado de run_handlers para tener más homogeneidad en la forma 
    /// utilizar los handle.
    /// Se podría hacer que el pool de hilos solo hiciese context.handler->process(context.request, context.response); 
    /// y esperar al future por que mi thread pool está hecho para esto, pero el hilo igualmente deberia esperar 
    /// a que el future estuviese disponible entonces no habría concurencia como tal solo una division del trabajo.
    /// 
    /// De esta manera el hilo de run_handlers() solo encola funciones sin parar y el pool de hilos y el hilo de 
    /// lua se encargan de ejecutar esas funciones ellos mientras tanto.
    /// </summary>
    /// <param name="context"></param>
    void HttpServer::handler_process(std::shared_ptr<ConnectionContext> context)
    {
        if (context->state != ConnectionContext::CLOSED)
        {
            ConnectionContext::LifecycleState expected = ConnectionContext::LifecycleState::IDLE;
            if (context->lifecycle.compare_exchange_strong(expected, ConnectionContext::LifecycleState::PROCESSING))
            {
                
                bool finished = context->handler->process(context->request, context->response);
                context->lifecycle.store(ConnectionContext::LifecycleState::IDLE);
                if (finished)
                {
                    /// en caso de que halla acabado 
                    context->state.store(ConnectionContext::WRITING_RESPONSE_HEADER);
                    ++pending_writes_count;
                    transfer_cv.notify_one();
                }
                else
                {
                    ready_handlers_queue.push(context);
                }
            }
        }
           
    }

    
}
