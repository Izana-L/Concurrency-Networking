
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <iostream>
#include "LuaServerApplication.hpp"

namespace argb
{

    namespace
    {

        Sqlite::Value make_sqlite_argument_from_lua (lua::Value argument, int argument_index)
        {
            if (argument.is<lua::Nil    > ()) return nullptr;
            if (argument.is<lua::Boolean> ()) return argument.to<bool> ();
            if (argument.is<lua::Number > ()) return argument.to<double> ();
            if (argument.is<lua::String > ()) return argument.to<std::string> ();

            throw std::invalid_argument
            (
                "Invalid argument type for query at position " + std::to_string (argument_index) + ". Supported types are: nil, boolean, number, string."
            );
        }

        std::vector<Sqlite::Value> collect_lua_arguments_for_sqlite (const lua::Value & arguments)
        {
            if (arguments.is<lua::Nil> ())
            {
                return {};
            }

            if (not arguments.is<lua::Table> ())
            {
                throw std::invalid_argument("Parameter 'arguments' must be an array-like table or nil.");
            }

            std::map<int, Sqlite::Value> argument_values;

            int max_argument_index = 0;

            arguments.for_each_in_table
            (
                [&] (lua::Value key, lua::Value value)
                {
                    if (not key.is<lua::Integer> ())
                    {
                        throw std::invalid_argument("Parameter 'arguments' must use only positive integer keys.");
                    }

                    int argument_index = key.to<int> ();

                    if (argument_index <= 0)
                    {
                        throw std::invalid_argument("Parameter 'arguments' must use only positive integer keys.");
                    }

                    if (argument_index > max_argument_index)
                    {
                        max_argument_index = argument_index;
                    }

                    argument_values.insert_or_assign
                    (
                        argument_index,
                        make_sqlite_argument_from_lua (value, argument_index)
                    );
                }
            );

            std::vector<Sqlite::Value> result(max_argument_index, nullptr);

            for (auto & [argument_index, argument_value] : argument_values)
            {
                result[argument_index - 1] = std::move (argument_value);
            }

            return result;
        }

    }

    // TO DO: we create request and response tables which store native object pointers.
    // If Lua code keeps references to these tables after the request is processed, it will be able to access the
    // native objects after they are destroyed, which is undefined behavior. We should find a way to prevent this,
    // for example by making the tables invalid after the request is processed or by using a different mechanism.
    bool LuaServerApplication::RequestHandler::process (const HttpRequest & request, HttpResponse & response)
    {
        try
        {
            HttpResponse::Serializer        response_serializer(response);
            LuaHttpResponseSerializerBridge response_bridge    (response_serializer);

            auto  request_table = server.virtual_machine.newTable ();
            auto response_table = server.virtual_machine.newTable ();

             request_table.set ("__native_object_type",    native_object_type_name<HttpRequest> ());
             request_table.set ("__native_object_pointer", static_cast<lua::Pointer>(const_cast<HttpRequest *>(&request)));
            response_table.set ("__native_object_type",    native_object_type_name<LuaHttpResponseSerializerBridge> ());
            response_table.set ("__native_object_pointer", static_cast<lua::Pointer>(&response_bridge));

            request_table = server.virtual_machine["setmetatable"].call
            (
                request_table,
                server.virtual_machine["__http_request_metatable"]
            );

            response_table = server.virtual_machine["setmetatable"].call
            (
                response_table,
                server.virtual_machine["__http_response_metatable"]
            );

            //coroutine.unref ().call (request_table, response_table);
            coroutine.resume(request_table, response_table);
            return coroutine.isDead();
        }
        catch (const lua::RuntimeError & error)
        {
            std::cout << "Lua runtime error: " << error.what () << std::endl;
        }
        catch (const std::exception & exception)
        {
            std::cout << "Standard exception: " << exception.what () << std::endl;
        }
        catch (...)
        {
            std::cout << "Unknown exception was caught at LuaServerApplication::RequestHandler::process()." << std::endl;
        }

        send_plain_text_response (response, 500, "Internal Server Error");

        return true;
    }

    LuaServerApplication::LuaServerApplication(const std::string_view & script_path_string)
    {
        std::filesystem::path script_path(script_path_string);

        // Check if the script file exists and is a regular file:

        if (not std::filesystem::exists (script_path) || not std::filesystem::is_regular_file (script_path))
        {
            throw std::runtime_error("Script file does not exist or cannot be accessed or excecuted: " + script_path.string ());
        }

        // Set the base path to the directory containing the script file:

        base_path = std::filesystem::absolute (script_path).parent_path ();

        // Create the bridge between Lua and C++:

        create_lua_bridge ();

        // Load the starting Lua script:

        virtual_machine.doFile (script_path.string ());
    }

    HttpRequestHandler::Ptr LuaServerApplication::create_handler (HttpRequest::Method method, std::string_view path)
    {
        auto method_index = static_cast<int>(method);

        if (method_index >= 0 && method_index < static_cast<int>(HttpRequest::Method::COUNT))
        {
            auto & endpoints_by_path = endpoints[method_index];

            auto iterator = endpoints_by_path.upper_bound (path);

            if (iterator != endpoints_by_path.begin ())
            {
                --iterator;

                const auto & key = iterator->first;

                if (path.starts_with (key) && (path.length () == key.length () || path[key.length ()] == '/' || key.back () == '/'))
                {
                    auto coroutine = std::move(iterator->second);
                    endpoints_by_path.erase(iterator);
                    return { std::make_unique<RequestHandler>(*this, std::move(coroutine)) };
                }
            }
        }

        return {};
    }

    void LuaServerApplication::route (const std::string & method_string, const std::string & path, lua::Value endpoint)
    {
        if (path.empty () || path.front () != '/')
        {
            throw std::invalid_argument("Parameter 'path' must be a non-empty string starting with '/'.");
        }

        if (not endpoint.is<lua::Callable> ())
        {
            throw std::invalid_argument("Parameter 'endpoint' must be a Lua function.");
        }

        auto method = HttpRequest::Parser::method_from_string (method_string);

        if (method == HttpRequest::Method::UNDEFINED)
        {
            throw std::invalid_argument("Invalid HTTP method: " + method_string + ".");
        }

        auto & endpoints_by_path = endpoints[static_cast<size_t>(method)];

        // Keys ending with '/' act as prefix routes (e.g. "/users/" matches "/users/1").
        // Keys without a trailing '/' match exactly or as a path-segment prefix.
        // Both forms are kept as-is so they coexist as distinct entries in the map.
        Lua_Coroutine coroutine(virtual_machine, endpoint);
        if (coroutine.isError()) {
            throw std::invalid_argument("Parameter 'endpoint' is not a callable function.");
        }

        endpoints_by_path.insert_or_assign (std::string(path), std::move (coroutine));
    }

    void LuaServerApplication::create_lua_bridge ()
    {
        create_bridge_for_server        ();
        create_bridge_for_http_request  ();
        create_bridge_for_http_response ();
        create_bridge_for_sqlite        ();
    }

    void LuaServerApplication::create_bridge_for_server ()
    {
        auto server_bridge = virtual_machine.newTable ();

        server_bridge.set
        (
            "route",
            [this] (lua::Value method_value, lua::Value path_value, lua::Value endpoint_value)
            {
                if (not method_value.is<lua::String> ()) throw std::invalid_argument("Parameter 'method' must be a string.");
                if (not   path_value.is<lua::String> ()) throw std::invalid_argument("Parameter 'path' must be a string.");

                route (method_value.toString (), path_value.toString (), endpoint_value);
            }
        );

        virtual_machine.set ("server", server_bridge);
    }

    void LuaServerApplication::create_bridge_for_http_request ()
    {
        auto http_request_bridge = virtual_machine.newTable ();

        create_method_bridge<HttpRequest> (http_request_bridge, "get_protocol", &HttpRequest::get_protocol);
        create_method_bridge<HttpRequest> (http_request_bridge, "get_method",   &HttpRequest::get_method  );
        create_method_bridge<HttpRequest> (http_request_bridge, "get_path",     &HttpRequest::get_path    );
        create_method_bridge<HttpRequest> (http_request_bridge, "get_fragment", &HttpRequest::get_fragment);
        create_method_bridge<HttpRequest> (http_request_bridge, "get_header",   &HttpMessage::get_header  );
        create_method_bridge<HttpRequest> (http_request_bridge, "get_query",    &HttpRequest::get_query   );
        create_method_bridge<HttpRequest> (http_request_bridge, "get_body",     &HttpMessage::get_body    );

        auto http_request_metatable = virtual_machine.newTable ();

        http_request_metatable.set ("__index",      http_request_bridge);
        http_request_metatable.set ("__metatable", "protected");

        http_request_metatable.set
        (
            "__newindex",
            [this](lua::Value, lua::Value, lua::Value)
            {
                throw std::runtime_error("HTTP request is read-only.");
            }
        );

        virtual_machine.set ("__http_request_metatable", http_request_metatable);
    }

    void LuaServerApplication::create_bridge_for_http_response ()
    {
        auto http_response_bridge = virtual_machine.newTable ();

        create_method_bridge<LuaHttpResponseSerializerBridge> (http_response_bridge, "status",     &LuaHttpResponseSerializerBridge::status    );
        create_method_bridge<LuaHttpResponseSerializerBridge> (http_response_bridge, "header",     &LuaHttpResponseSerializerBridge::header    );
        create_method_bridge<LuaHttpResponseSerializerBridge> (http_response_bridge, "end_header", &LuaHttpResponseSerializerBridge::end_header);
        create_method_bridge<LuaHttpResponseSerializerBridge> (http_response_bridge, "body",       &LuaHttpResponseSerializerBridge::body      );

        auto http_response_metatable = virtual_machine.newTable ();

        http_response_metatable.set ("__index",      http_response_bridge);
        http_response_metatable.set ("__metatable", "protected");

        http_response_metatable.set
        (
            "__newindex",
            [this] (lua::Value, lua::Value, lua::Value)
            {
                throw std::runtime_error("HTTP response fields cannot be assigned directly.");
            }
        );

        virtual_machine.set ("__http_response_metatable", http_response_metatable);
    }

    void LuaServerApplication::create_bridge_for_sqlite ()
    {
        auto db_table = virtual_machine.newTable ();

        db_table.set
        (
            "execute",
            [this] (std::string sql_code, lua::Value lua_arguments)
            {
                try
                {
                    auto sqlite_arguments = collect_lua_arguments_for_sqlite (lua_arguments);

                    database ().execute (sql_code, std::span<const Sqlite::Value> (sqlite_arguments));
                }
                catch (const std::exception & exception)
                {
                    throw std::runtime_error(std::string("Database error: ") + exception.what ());
                }
            }
        );

        db_table.set
        (
            "query",
            [this] (std::string sql_code, lua::Value lua_arguments) -> lua::Value
            {
                try
                {
                    auto sqlite_arguments = collect_lua_arguments_for_sqlite (lua_arguments);

                    return make_sqlite_row_table (database ().query (sql_code, std::span<const Sqlite::Value> (sqlite_arguments)));
                }
                catch (const std::exception & exception)
                {
                    throw std::runtime_error(std::string("Database error: ") + exception.what ());
                }
            }
        );

        virtual_machine.set ("database", db_table);
    }

    // TO DO: we create a Lua table which stores a native object pointer to the Row object and we store the Row object
    // in a map in the LuaServerApplication instance. When Lua GC collects the table, we remove the corresponding Row
    // object from the map. This way we ensure that the Row object is kept alive as long as Lua can access it and it is
    // destroyed when Lua can no longer access it.
    // However, this approach has some limitations. The Row lifetime is tied to the Lua table, so if Lua code keeps
    // references to the table after the query is processed, it will keep the Row object alive and prevent it from being
    // finalized. We should find a way to prevent this, for example by making the table invalid after the query is
    // processed or by using a different mechanism.
    lua::Value LuaServerApplication::make_sqlite_row_table (Sqlite::Row row)
    {
        auto  bridge     = std::make_unique<LuaSqliteRowBridge> (std::move (row));
        auto  bridge_ptr = bridge.get ();

        database_row_bridges.emplace (bridge_ptr, std::move (bridge));

        auto row_table = virtual_machine.newTable ();

        row_table.set ("__native_object_type",    native_object_type_name<LuaSqliteRowBridge> ());
        row_table.set ("__native_object_pointer", static_cast<lua::Pointer>(bridge_ptr));

        // Plant a sentinel lambda inside the row table. When Lua GCs the row table, this functor
        // userdata becomes unreachable and its __gc metamethod fires, destroying the lambda and
        // invoking the custom deleter, which removes the bridge from the map:

        auto gc_hook = std::shared_ptr<void>
        (
            bridge_ptr,
            [this] (LuaSqliteRowBridge * key) { database_row_bridges.erase (key); }
        );

        row_table.set ("__gc_sentinel", [gc_hook] () {});

        {
            auto row_method_table = virtual_machine.newTable ();

            create_method_bridge<LuaSqliteRowBridge> (row_method_table, "advance",     &LuaSqliteRowBridge::advance    );
            create_method_bridge<LuaSqliteRowBridge> (row_method_table, "get_integer", &LuaSqliteRowBridge::get_integer);
            create_method_bridge<LuaSqliteRowBridge> (row_method_table, "get_string",  &LuaSqliteRowBridge::get_string );
            create_method_bridge<LuaSqliteRowBridge> (row_method_table, "get_real",    &LuaSqliteRowBridge::get_real   );

            auto row_metatable = virtual_machine.newTable ();

            row_metatable.set ("__index",      row_method_table);
            row_metatable.set ("__metatable", "protected"      );

            row_metatable.set
            (
                "__newindex",
                [this] (lua::Value, lua::Value, lua::Value)
                {
                    throw std::runtime_error("Row fields cannot be assigned directly.");
                }
            );

            row_table = virtual_machine["setmetatable"].call (row_table, row_metatable);
        }

        return row_table;
    }

}
