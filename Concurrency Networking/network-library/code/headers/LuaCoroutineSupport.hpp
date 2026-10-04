#pragma once




#include <LuaState.hpp>  // Asegura que tengas lua_State y lua_isthread/lua_tothread

namespace lua {
    namespace stack {

        // Especialización explícita para lua::Coroutine
        template <>
        inline lua::Coroutine read<lua::Coroutine>(lua_State* L, int index) {
            // Verificamos que el valor en la pila sea realmente un hilo
            if (!lua_isthread(L, index)) {
                // Lanza una excepción acorde al resto de la biblioteca (ajusta si usan otro tipo)
                throw lua::RuntimeError(L, "Expected a coroutine on the stack");
            }

            // Extraemos el lua_State* del hilo
            lua_State* thread = lua_tothread(L, index);

            // Construimos y devolvemos un lua::Coroutine (ajusta según el constructor real)
            return lua::Coroutine(thread);
        }

    } // namespace stack
} // namespace lua