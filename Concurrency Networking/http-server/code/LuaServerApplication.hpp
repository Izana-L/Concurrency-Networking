
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <array>
#include <filesystem>
#include <HttpRequestHandler.hpp>
#include <HttpRequestHandlerFactory.hpp>
#include <HttpResponse.hpp>
#include <LuaState.hpp>
#include "LuaTypeAdapter.hpp"

#include <map>
#include <memory>
#include "Sqlite.hpp"
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace argb
{

    /** This is responsible for managing the Lua virtual machine, handling HTTP requests by routing them to Lua-defined
      * endpoints, and providing a bridge between Lua and C++ for database access and HTTP response generation.
      * It implements the HttpRequestHandlerFactory interface to create request handlers based on the registered Lua
      * endpoints, and it maintains the state of the application, including the Lua virtual machine, database connection,
      * and endpoint mappings.
      */
    class LuaServerApplication : public HttpRequestHandlerFactory
    {


        using VirtualMachine           = lua::State;
        using Database                 = Sqlite;
        using DatabasePtr              = std::unique_ptr<Database>;
        using Lua_Coroutine            = lua::Coroutine;
        using EndpointsByPath          = std::map<std::string, Lua_Coroutine, std::less<>>;
        using EndpointsByMethodAndPath = std::array<EndpointsByPath, static_cast<size_t>(HttpRequest::Method::COUNT)>;

        /** This class implements the logic for handling HTTP requests that are routed to Lua-defined endpoints. It
          * manages the state of the request processing, including invoking the appropriate Lua function for the
          * requested endpoint and generating appropriate HTTP responses based on the outcome of these invocations.
          */
        class RequestHandler : public HttpRequestHandler
        {
            LuaServerApplication & server;
            Lua_Coroutine         coroutine;
            

        public:

            /** Constructs a RequestHandler for the specified LuaServerApplication and endpoint. It initializes the
              * handler with references to the server and the Lua endpoint that it will invoke when processing requests.
              */
            RequestHandler(LuaServerApplication& server, Lua_Coroutine&& endpoint)
                : server(server)
                , coroutine(std::move(endpoint))
            {
            }

            /** Processes the incoming HTTP request and generates an appropriate response by invoking the corresponding
              * Lua function for the requested endpoint. It handles any exceptions that may occur during the Lua function
              * invocation and generates a generic error response if necessary.
              * The method returns true when the response is fully ready to be sent back to the client, or false if the
              * handler is still processing (e.g., waiting for Lua function execution).
              * @param request The incoming HTTP request that triggered this handler.
              * @param response The HTTP response object that will be populated with the appropriate status, headers,
              *     and body based on the processing of the request and the outcome of the Lua function invocation.
              * @return true if the response is fully ready to be sent back to the client, or false if the handler is
              *     still processing (e.g., waiting for Lua function execution).
              */
            bool process (const HttpRequest & request, HttpResponse & response) override;
            HandlerType getHandlerType() const override { return HandlerType::LUA; }

        };

        /** This class serves as a bridge between Lua and C++ for generating HTTP responses. It provides methods that
          * can be called from Lua to manipulate the HTTP response, such as setting the status code, adding headers,
          * and writing the response body.
          */
        class LuaHttpResponseSerializerBridge
        {
            HttpResponse::Serializer * serializer;

        public:

            LuaHttpResponseSerializerBridge(HttpResponse::Serializer & serializer)
                : serializer(&serializer)
            {
            }

            void status (int value)
            {
                serializer->status (value);
            }

            void header (const std::string & name, const std::string & value)
            {
                serializer->header (name, value);
            }

            void end_header ()
            {
                serializer->end_header ();
            }

            void body (const std::string & content)
            {
                serializer->body (std::span<const char>(content.data (), content.size ()));
            }
        };

        /** This class serves as a bridge between Lua and C++ for accessing SQLite database rows. It provides methods
          * that can be called from Lua to iterate over rows and retrieve column values by index.
          */
        class LuaSqliteRowBridge
        {
            Sqlite::Row row;

        public:

            LuaSqliteRowBridge(Sqlite::Row row) : row(std::move (row))
            {
            }

            bool        advance     ()          { return row.advance ();                   }
            int         get_integer (int index) { return row.get<int        > (index - 1); }
            std::string get_string  (int index) { return row.get<std::string> (index - 1); }
            double      get_real    (int index) { return row.get<double     > (index - 1); }
        };

        using DatabaseRowBridges = std::map<LuaSqliteRowBridge *, std::unique_ptr<LuaSqliteRowBridge>>;

    private:

        DatabasePtr              database_ptr;
        DatabaseRowBridges       database_row_bridges;
        VirtualMachine           virtual_machine;
        EndpointsByMethodAndPath endpoints;
        std::filesystem::path    base_path;
    public:

        /** Constructs a LuaServerApplication instance by loading the specified Lua script and initializing the Lua
          * virtual machine.
          */
        LuaServerApplication(const std::string_view & script_path_string);

        /** Creates an HTTP request handler for the specified HTTP method and request path by looking up the corresponding
          * Lua endpoint if it exists. If a matching endpoint is found, a RequestHandler instance is created and returned
          * to handle the request. Otherwise, a null handler is returned, indicating that no handler is available for the
          * requested method and path.
          */
        HttpRequestHandler::Ptr create_handler (HttpRequest::Method method, std::string_view path) override;

        /** Registers a Lua endpoint for the specified HTTP method and request path. The endpoint is represented by a Lua
          * function that will be invoked when a matching request is received.
          */
        void route (const std::string & method_string, const std::string & path_string, lua::Value endpoint);

    private:

        /** Provides access to the single instance of the Database object used by each instance of LuaServerApplication.
          * It doesn't create the Database object until it's actually needed, if ever. The database will be closed and
          * resources will be released when the LuaServerApplication instance is destroyed.
          * @return A reference to the Database object used by the application.
          */
        Database & database ()
        {
            if (not database_ptr) database_ptr = std::make_unique<Database> (base_path / "database.bin");
            return *database_ptr;
        }

        void create_lua_bridge ();

        void create_bridge_for_server        ();
        void create_bridge_for_http_request  ();
        void create_bridge_for_http_response ();
        void create_bridge_for_sqlite        ();

        lua::Value make_sqlite_row_table (Sqlite::Row row);

    private:

        template<class WRAPPED_CLASS_TYPE>
        static constexpr const char * native_object_type_name ();

        template<class WRAPPED_CLASS_TYPE, class CLASS_TYPE = WRAPPED_CLASS_TYPE>
        CLASS_TYPE * get_native_object_pointer (lua::Value table);

        template<class WRAPPED_CLASS_TYPE, class CLASS_TYPE, typename RETURN_TYPE, typename ... ARGUMENTS>
        void create_method_bridge
        (
            lua::Value & table,
            const std::string & method_name,
            RETURN_TYPE (CLASS_TYPE::*method_pointer) (ARGUMENTS...)
        );

        template<class WRAPPED_CLASS_TYPE, class CLASS_TYPE, typename RETURN_TYPE, typename ... ARGUMENTS>
        void create_method_bridge
        (
            lua::Value & table,
            const std::string & method_name,
            RETURN_TYPE (CLASS_TYPE::*method_pointer) (ARGUMENTS...) const
        );

    };

    template<>
    constexpr const char * LuaServerApplication::native_object_type_name<HttpRequest> ()
    {
        return "HttpRequest";
    }

    template<>
    constexpr const char * LuaServerApplication::native_object_type_name<LuaServerApplication::LuaHttpResponseSerializerBridge> ()
    {
        return "HttpResponseSerializer";
    }

    template<>
    constexpr const char * LuaServerApplication::native_object_type_name<LuaServerApplication::LuaSqliteRowBridge> ()
    {
        return "SqliteRow";
    }

    template<class WRAPPED_CLASS_TYPE, class CLASS_TYPE>
    CLASS_TYPE * LuaServerApplication::get_native_object_pointer (lua::Value table)
    {
        if (not table.is<lua::Table> ())
        {
            throw std::invalid_argument("Expected a native object table.");
        }

        auto type = table["__native_object_type"];

        if (not type.is<lua::String> ())
        {
            throw std::invalid_argument("Invalid native object type.");
        }

        if (type.toString () != native_object_type_name<WRAPPED_CLASS_TYPE> ())
        {
            throw std::invalid_argument("Invalid native object type.");
        }

        auto pointer_value = table["__native_object_pointer"];

        if (not pointer_value.is<lua::Pointer> ())
        {
            throw std::invalid_argument("Invalid native object pointer.");
        }

        auto pointer = pointer_value.to<lua::Pointer> ();

        if (pointer == nullptr)
        {
            throw std::invalid_argument("Invalid native object.");
        }

        return static_cast<CLASS_TYPE *>(pointer);
    }

    template<class WRAPPED_CLASS_TYPE, class CLASS_TYPE, typename RETURN_TYPE, typename ... ARGUMENTS>
    void LuaServerApplication::create_method_bridge
    (
        lua::Value & table,
        const std::string & method_name,
        RETURN_TYPE (CLASS_TYPE::*method_pointer) (ARGUMENTS...)
    )
    {
        table.set
        (
            method_name,
            [this, method_pointer] (lua::Value self, typename LuaTypeAdapter<ARGUMENTS>::LuaType ... arguments)
            {
                auto * object = get_native_object_pointer<WRAPPED_CLASS_TYPE, CLASS_TYPE> (self);

                if constexpr (std::is_void_v<RETURN_TYPE>)
                {
                    (object->*method_pointer) (LuaTypeAdapter<ARGUMENTS>::adapt_from_lua (arguments)...);
                }
                else
                {
                    return LuaTypeAdapter<RETURN_TYPE>::adapt_for_lua
                    (
                        (object->*method_pointer) (LuaTypeAdapter<ARGUMENTS>::adapt_from_lua (arguments)...)
                    );
                }
            }
        );
    }

    template<class WRAPPED_CLASS_TYPE, class CLASS_TYPE, typename RETURN_TYPE, typename ... ARGUMENTS>
    void LuaServerApplication::create_method_bridge
    (
        lua::Value & table,
        const std::string & method_name,
        RETURN_TYPE (CLASS_TYPE::*method_pointer) (ARGUMENTS...) const
    )
    {
        table.set
        (
            method_name,
            [this, method_pointer] (lua::Value self, typename LuaTypeAdapter<ARGUMENTS>::LuaType ... arguments)
            {
                auto * object = get_native_object_pointer<WRAPPED_CLASS_TYPE, CLASS_TYPE> (self);

                if constexpr (std::is_void_v<RETURN_TYPE>)
                {
                    (object->*method_pointer) (LuaTypeAdapter<ARGUMENTS>::adapt_from_lua (arguments)...);
                }
                else
                {
                    return LuaTypeAdapter<RETURN_TYPE>::adapt_for_lua
                    (
                        (object->*method_pointer) (LuaTypeAdapter<ARGUMENTS>::adapt_from_lua (arguments)...)
                    );
                }
            }
        );
    }

}
