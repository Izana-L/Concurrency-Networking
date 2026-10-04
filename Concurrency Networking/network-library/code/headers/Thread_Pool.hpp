#pragma once
#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <future>
#include <condition_variable>
#include <functional>
#include <vector>
#include <queue>
#include <type_traits>

namespace argb
{
    /// <summary>
    /// Se utiliza Task como std::packaged_task<void()>; para mejorar la lectura del codigo
    /// </summary>
    using Task = std::packaged_task<void()>;
    class Thread_Pool
    {

    private:
        
        std::atomic_bool is_active{ true }; // esta atomic bool se encarga de determinar se este thread pool esta activo
        std::atomic_size_t busy_workers{ 0 }; // esta varible cuenta cuantos hilos estas trabajando 
        std::vector<std::jthread> pool; // esta es la pool de hilos
        std::deque<Task> pending_jobs; // es la deque de tareas 
        std::condition_variable cv; // esta condition_variable sirve para notificar a los demás hilos cuando pueden se ha añadido una tarea a la cola 
        std::condition_variable idle_cv; // esta condition_variable para notificar que todos los hilos estan libres
        std::mutex guard;
        
       
    public:
        /// <summary>
        /// Se inicializa todos los hilos del thread pool ejecutando la funcion run
        /// </summary>
        /// <param name="num_threads"></param>
        Thread_Pool(int num_threads = 1)
        {
            for (int i = 0; i < num_threads; i++) {
                pool.emplace_back(&Thread_Pool::run, this);
            }
        }
        ~Thread_Pool()
        {
            stop();
        }
        /// <summary>
        /// Se ha eliminado la copia y el movimiento por que no se debria hacer compia de un thread pool y moverlo 
        /// no tiene sentido.
        /// </summary>
        /// <param name=""></param>
        Thread_Pool(const Thread_Pool&) = delete;
        Thread_Pool(Thread_Pool&&) = delete;

        Thread_Pool& operator=(const Thread_Pool&) = delete;
        Thread_Pool& operator=(Thread_Pool&&) = delete;

        /// <summary>
        /// Funcion para encolar funcines al pool de hilos 
        /// </summary>
        /// <param name="job"></param>
        void post(Task  job) 
        {
            std::unique_lock lock(guard);
            pending_jobs.emplace_back(move(job));
            cv.notify_one();
        }
        /// <summary>
        /// Funcion para parar la pool de hilos con antes liberara la cola de tareas.
        /// </summary>
        void stop()
        {
            {
                std::lock_guard lock(guard);
                pending_jobs.clear();
            }
            wait_for_idle();
            is_active = false;
            cv.notify_all();
        }
        /// <summary>
        /// funcion para esperar a que todos los hilos esten sin trabajo
        /// </summary>
        void wait_for_idle() 
        {
            std::unique_lock lock(guard);
            idle_cv.wait(lock, [this] {return pending_jobs.empty() && busy_workers == 0; });
        }

        /// <summary>
        /// espera a que todos los hilos esten si trabajo y desactiva el pool
        /// </summary>
        void shutdown() 
        {
            if (!is_active) return; 
                wait_for_idle();
                is_active = false;
                cv.notify_all(); 
        }

    private:
        /// <summary>
        /// Esta función se encarga de sacar tareas de deque de tareas y ejecutarlas 
        /// </summary>
        void run() noexcept
        {
            while (is_active)
            {
                thread_local Task job;
                {
                    std::unique_lock lock(guard);
                    cv.wait(lock, [&] { return !pending_jobs.empty() || !is_active; });
                    if (!is_active) break;
                    job.swap(pending_jobs.front());
                    pending_jobs.pop_front();
                }

                // esta parte se encarga de contar cuantos hilos estan trabajando 
                ++busy_workers;
                job();
                --busy_workers;

                // Si ya no hay tareas pendientes y nadie trabaja, notificamos a wait_for_idle
                if (busy_workers == 0 && pending_jobs.empty())
                {
                    idle_cv.notify_all();
                }
            }
        }
    };
    /// <summary>
    /// esto es se utiliza como flag para sobrecargar el metodo post
    /// </summary>
    struct use_future_tag {};

    template <class Fn>
    constexpr auto use_future(Fn&& function) {
        return make_tuple(use_future_tag{}, forward<Fn>(function));
    }
    /// <summary>
    /// la función post esta fuera de la clase Thread Pool por que esta creada para 
    /// que puedas asignar que pool quieres que se encargue la función que estas encolando. 
    /// Ya que esta pensado para que se pueda utilzar más de un thread pool aun que en este 
    /// proyecto solo se use uno.
    /// </summary>
    /// <typeparam name="Executor"></typeparam>
    /// <typeparam name="Fn"></typeparam>
    /// <param name="executor"></param>
    /// <param name="function"></param>
    template <class Executor, class Fn>
    void post(Executor& executor, Fn&& function)
    {
        using return_type = decltype(function());// se comprueba por si acaso el tipo de la funcion para debuguear si hay algun error
        static_assert(std::is_void_v<return_type>, "posting functions with return types must be used with \"use_future\" tag.");
        Task task(std::forward<Fn>(function)); // se envuelve la funcion con Task
        executor.post(move(task)); // y se encola 
    }
    /// <summary>
    /// En este contexto, post devuelve un std::future que el usuario debe esperar para obtener 
    /// el resultado de la operación asíncrona. Ignorar ese futuro podría hacer que el programa 
    /// pierda la capacidad de sincronizar o recuperar el valor, por lo que se marca como [[nodiscard]] para evitar errores.
    /// </summary>
    /// <typeparam name="Executor"></typeparam>
    /// <typeparam name="Fn"></typeparam>
    /// <param name="executor"></param>
    /// <param name="tpl"></param>
    /// <returns></returns>
    template <class Executor, class Fn>
    [[nodiscard]] decltype(auto) post(Executor& executor, std::tuple<use_future_tag, Fn>&& tpl)
    {
        using namespace std;
        using return_type = invoke_result_t<Fn>;
        auto&& [_, function] = tpl;// solo es necesario function asi se ignora la flag
        if constexpr (is_void_v<return_type>)// si es void se ejecuta esta parte que es el mismo codigo que si no tuviese future
        {
            Task task(forward<Fn>(function));

            auto ret_future = task.get_future();
            executor.post(move(task));
            return ret_future;
        }
        else
        {
            //Esta parte de codigo tiene la función de encapsular la función original Fn (que devuelve un valor no void) 
            //en un objeto funtor que pueda ser invocado posteriormente con un shared_ptr<promise<return_type>> para establecer el resultado.
            struct forwarder_t {
                forwarder_t(Fn&& fn) : task(forward<Fn>(fn)) {}
                void operator()(shared_ptr<promise<return_type>> promise)
                {
                    promise->set_value(task());
                }
            private:
                decay_t<Fn> task;
            } forwarder(forward<Fn>(function));

            auto _promise = make_shared<promise<return_type>>();// se define el tipo de promise
            auto ret_future = _promise->get_future();// se obtiene el future 
            Task task([my_promise = move(_promise), forwarder = move(forwarder)]() mutable {forwarder(my_promise); });// se transforma la función 
            //original con valor de retorno no-void en una tarea sin retorno que, cuando es ejecutada, materializa el resultado en un promise cuyo future ya ha sido devuelto al llamante.
            executor.post(move(task)); // se encola
            return ret_future;
        }
    }
}