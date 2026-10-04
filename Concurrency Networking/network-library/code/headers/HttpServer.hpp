
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequestHandlerFactory.hpp>
#include <AtomicSignal.hpp>
#include <Thread_Directory.hpp>
#include <ConnectionContext.hpp>
#include <Circular_Queue.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <shared_mutex>

namespace argb
{

    class HttpServer
    {
   
        /// Se ha movido la clase ConnectionContext a otro archivo con el mismo nombre para estructurar mejor el código 
        /// Para saber si un HttpRequestHandler o un RequestHandler
   
          
        class RequestHandlerManager
        {
            using HandlerFactoryContainer = std::vector<HttpRequestHandlerFactory *>;
            
            HandlerFactoryContainer handler_factories;

        public:

            void register_handler_factory (HttpRequestHandlerFactory & factory)
            {
                handler_factories.push_back (&factory);
            }

            HttpRequestHandler::Ptr create_handler (HttpRequest::Method method, std::string_view request_path) const;
        };

        struct ListenerScopeGuard
        {
            TcpListener & listener;

            ~ListenerScopeGuard() { listener.close (true); }
        };
        
        using  IoBuffer         = std::array<std::byte, 4096>;
        /// <summary>
        /// Para facilitar la lectura del codigo se utilizan estos sobre nombres.
        /// </summary>
        using Connections_Queue = Circular_Queue<std::shared_ptr<ConnectionContext>>;
        using Handle_Queue      = Circular_Queue<TcpSocket::Handle>;
        using Conection_Vector  = std::vector<std::shared_ptr<ConnectionContext>>;
        /// <summary>
        /// Estas constantes se utilizan para no tener valores numericos asignados y en caso de 
        /// querer modificarlo tener los todos agrupados.
        /// </summary>
        static constexpr size_t num_size_queue  = 256;
        static constexpr std::chrono::seconds connection_timeout{ 10 };

    private:
        /// <summary>
        /// este vector de conexiones solo se utiliza en el hilo de aceptar y cerrar conexiones a si que no necesita ningún sistema 
        /// de protección concurrente
        /// </summary>
        Conection_Vector    connections_vector;
        /// <summary>
        /// Sirve para que el hilo aceptador encole las conexiones de forma asincrona para 
        /// pasarselas al hilo que tranfiere los datos de las conexiones y este las procese
        /// </summary>
        Connections_Queue   new_connections_queue;
        /// <summary>
        /// Esta queue tiene la funcion de obtenr las conexiones que esten en el estado
        /// RUNNING_HANDLER y pasarlos a run_handlers() para que este los procese
        /// </summary>
        Connections_Queue   ready_handlers_queue;
        /// <summary>
        /// La única funcion de esta AtomicSignal es ayudar a la concurrencia de hilos 
        /// entre el hilo de concurrent_transfer_data y pool de hilos.
        /// 
        /// </summary>
        AtomicSignal        pending_writes_count;

        TcpListener             listener;
        RequestHandlerManager   request_handler_manager;
        std::atomic<bool>       running{};
  
        /// transfer_mutex y transfer_cv sirven para notificar al hilo llamado 
        ///worker_thread si se hay trabajo por hacer o no

        std::mutex              transfer_mutex;
        std::condition_variable transfer_cv;

        /// <summary>
        /// Al ser un singleton se guarda la refencia de este
        /// </summary>
        Thread_Directory& thread_directory;

    public:
        /// <summary>
        /// Se obtiene la instancia de Thread_Directory y se inicializan las queues
        /// </summary>
        HttpServer() : thread_directory(Thread_Directory::getInstance()), 
                       ready_handlers_queue(num_size_queue),
                       new_connections_queue(num_size_queue)
                      
        {
        }

        void register_handler_factory (HttpRequestHandlerFactory & factory)
        {
            request_handler_manager.register_handler_factory (factory);
        }

        void run (const Port& local_port)
        {
            run (Address::any, local_port);
        }

        void run (const Address & local_address, const Port & local_port);
        /// <summary>
        /// Se cancela las queues y se paran la ejecucion de todos los hilos
        /// </summary>
        void stop ()
        {
            running = false;
            thread_directory.stop_all_threads();
            ready_handlers_queue.cancel();
            new_connections_queue.cancel();
        }
        
    private:
        // las funciones que tienen // las he ehcho una modificación  las que tienen //// son mias propias
        void accept_and_close_inactive_connections(std::stop_token stoken); /////
        void accept_connections ();//
        void close_inactive_connections();//

        void concurrent_transfer_data(std::stop_token stoken);////
        void transfer_data_by_one( ConnectionContext& context);//
        void receive_request (ConnectionContext & context);
        void write_response_header (ConnectionContext & context);
        void write_response_body (ConnectionContext & context);

        void run_handlers ();//
        void handler_process(std::shared_ptr<ConnectionContext> context);////

    };

}
