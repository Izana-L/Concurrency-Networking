#pragma once
#include <atomic>

namespace argb
{ 


	/// <summary>
	/// Simple contador atomico 
	/// </summary>
	class AtomicSignal
	{
		std::atomic<unsigned int> number;
	public :
		AtomicSignal() : number(0){}

		void reset()
		{
			number.store(0);
		}

		bool isCero() const 
		{
			return number.load() == 0;
		}
		bool isEmpty() const {
			return isCero();
		}
		unsigned int operator++() {
			return ++number;   
		}
		unsigned int increment() {
			return ++number;
		}
		unsigned int get() const {
			return number.load();
		}

	};
}