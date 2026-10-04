#pragma once
#include <Thread_Pool.hpp>
#include <Circular_Queue.hpp>
#include <latch>
namespace argb
{
	/// <summary>
	/// La clase Thread_Directory es la que se encarga de toda la logica de hilos.
	/// Se separa el thread pool con el resto de hilos por que el thread pool tiene una cantidad de hilos indefinidos.
	/// Ademas así los hilos tiene nombre se hace más facil de entender y modificar el codigo a futuro.
	/// Es un singleton para que no haya copias ni se construyan varios de esta clase.
	/// </summary>
	class Thread_Directory
	{
	public:

		using Task = std::function<void()>;

		Thread_Pool thread_pool; // este es el pool de hilos que se va encargar de ejecutar manejadores HTTP.
		std::jthread connections_manager_thread; // Hilo que se encarga de aceptar conexiones y cerrarlas.
		std::jthread worker_thread; // Hilo que se encarga tranferir los datos de los https.
		std::jthread lua_thread; // Hilo que se encarga de los handlers de lua. 
		std::latch wait_all_threads; // este latch se utiliza para que todos los hilos empiecen la ejecucion a la vez
		Circular_Queue<Task> lua_task_queue; // esta es la queue donde se encola los handler de lua

		/// <summary>
		/// Función para parar la ejecucion de todos los hilos y la queue
		/// </summary>
		void stop_all_threads()
		{
			connections_manager_thread.request_stop();
			worker_thread.request_stop();
			thread_pool.stop();
			lua_thread.request_stop();
		}
		/// <summary>
		/// Esta función tiene la logica de reducir el lacth a uno y esperar a que sea cero
		/// </summary>
		void wait_rest_threads()
		{
			wait_all_threads.count_down();
			wait_all_threads.wait();
		}

	private:
		/// <summary>
		/// El constructor de esta clase inicializa wait_all_threads a 4 ya que hay 4 threads que se ejecutan, 
		/// el hilo principal del programa y tres secundarios.
		/// Se inicia lua_thread dandole la funcion  consume_lua_funtions()
		/// </summary>
		Thread_Directory() : thread_pool(std::thread::hardware_concurrency()), lua_task_queue{100}, wait_all_threads{4}
		{
			lua_thread = std::jthread([this](std::stop_token stoken) {consume_lua_funtions(stoken); });
		}

		~Thread_Directory()
		{
			thread_pool.stop();
		}
		/// <summary>
		/// funcion que saca la funcion que se encarga de lua y la ejecuta.
		/// </summary>
		/// <param name="stoken"></param>
		void consume_lua_funtions(std::stop_token stoken) 
		{
			wait_rest_threads();
			while (!stoken.stop_requested()) 
			{
				auto task_opt = lua_task_queue.pop();  
				if (!task_opt) break;                    
				(*task_opt)();                           
			}
		}
	
	public:

		/// <summary>
		/// Para que sea un singleton se debe eliminar el constructor de copia y su asignación.
		///  Además de crear una funcion getInstance para pasarle la instancia de Thread_Directory.
		/// </summary>
		/// <param name=""></param>

		Thread_Directory(const Thread_Directory&) = delete;
		Thread_Directory& operator=(const Thread_Directory&) = delete;
		
		static Thread_Directory& getInstance() 
		{
			static Thread_Directory instance; // thread-safe desde C++11
			return instance;
		}
	};
}
