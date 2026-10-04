
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

extern "C"
{
    typedef struct sqlite3      sqlite3;
    typedef struct sqlite3_stmt sqlite3_stmt;
}

namespace argb
{

    /** This class provides a C++ wrapper around the SQLite database library, allowing for easy interaction with SQLite
      * databases from C++ code. It manages the database connection, prepares and executes SQL statements, and provides
      * a way to handle query results through the Row class.
      */
    class Sqlite
    {
        using Database    = sqlite3;                        ///< The underlying SQLite database connection type
        using Statement   = sqlite3_stmt;                   ///< The underlying SQLite statement type
        using byte_span   = std::span<const std::byte>;     ///< A span of bytes, used for binary data
        using string_view = std::string_view;               ///< A view of a string, used for efficient string handling

    public:

        /** This class represents a single row of results from a SQLite query. It provides methods to retrieve column
          * values by index and to advance to the next row in the result set.
          * The Row class manages the lifecycle of the underlying SQLite statement, ensuring that resources are properly
          * released when the Row object is destroyed or moved. It also provides type-safe access to column values
          * through the get method template, which checks bounds and throws exceptions for out-of-range access.
          */
        class Row 
        {
            friend class Sqlite;

            Statement * statement;

        private:

            /** Constructs a Row object with the given SQLite statement. The Row takes ownership of the statement and is
              * responsible for finalizing it when the Row is destroyed. The constructor is private, and Row objects can
              * only be created by the Sqlite class, ensuring that they are properly initialized with a valid statement.
              */
            Row(Statement * statement) : statement(statement)
            {
            }

            void finalize () noexcept;

        public:

            Row(const Row & ) = delete;
            Row & operator = (const Row & ) = delete;

            Row(Row && other) noexcept
            {
                this->statement = other.statement;
                other.statement = nullptr;
            }

            Row & operator = (Row && other) noexcept
            {
                if (this != &other) 
                {
                    this->finalize ();
                    this->statement = other.statement;
                    other.statement = nullptr;
                }

                return *this;
            }

           ~Row() noexcept
            {
                finalize ();
            }

        public:

            /** Retrieves the value of the specified column in the current row, converting it to the requested type. The
              * column index is zero-based, and the method checks bounds to ensure that the index is valid.
              * If the index is out of range, an exception is thrown. The method uses template specialization to handle
              * different types of column values, such as integers, strings, and binary data, providing a convenient and
              * type-safe way to access query results.
              * @tparam TYPE The type to which the column value should be converted. Supported types include nullptr_t,
              *     bool, int32_t, int64_t, double, std::string, and byte_span.
              * @param column_index The zero-based index of the column to retrieve.
              * @return The value of the specified column converted to the requested type.
              */
            template<typename TYPE>
            TYPE get (int column_index) const;

            /** Advances to the next row in the result set. This method should be called after retrieving column values
              * from the current row to move to the next row of results. It returns true if there is another row available,
              * or false if there are no more rows in the result set. The method also checks for errors during advancement
              * and throws exceptions if any issues are encountered.
              * @return true if there is another row available, or false if there are no more rows in the result set.
              */
            bool advance ();

        private:

            void check_bounds_at (int column_index) const;

        };

        /** This type represents a value that can be used as an argument in SQL statements. It is a variant that can hold
          * different types of values, including nullptr_t, bool, int32_t, int64_t, double, std::string, and byte_span.
          * This allows for flexible binding of parameters to SQL statements when executing queries or commands.
          */
        using Value = std::variant<std::nullptr_t, bool, int32_t, int64_t, double, std::string>;

    private:
        
        Database * database;

    public:

        /** Constructs a Sqlite object by opening a connection to the specified SQLite database file. The constructor takes
          * a filesystem path representing the location of the database file and attempts to open a connection using the
          * SQLite library. If the connection cannot be established, an exception is thrown with an appropriate error
          * message. The Sqlite class manages the lifecycle of the database connection, ensuring that it is properly
          * closed when the Sqlite object is destroyed or moved.
          * @param database_path A filesystem path representing the location of the SQLite database file to open.
          */
        Sqlite(std::filesystem::path database_path) : Sqlite(std::string_view(database_path.string ()))
        {
        }

        /** Constructs a Sqlite object by opening a connection to the specified SQLite database file. The constructor takes
          * a string view representing the path to the database file and attempts to open a connection using the SQLite
          * library. If the connection cannot be established, an exception is thrown with an appropriate error message.
          * The Sqlite class manages the lifecycle of the database connection, ensuring that it is properly closed when
          * the Sqlite object is destroyed or moved.
          * @param database_path A string view representing the path to the SQLite database file to open.
          */
        Sqlite(std::string_view database_path);

        /** Destructs the Sqlite object by closing the database connection and releasing any associated resources.
          */
       ~Sqlite();

        Sqlite(const Sqlite & ) = delete;
        Sqlite & operator = (const Sqlite & ) = delete;

        /** Movement constructor for the Sqlite class. It transfers ownership of the database connection from the source
          * object to the destination object, leaving the source object in a valid but unspecified state (with a null
          * database pointer).
          * @param other The source Sqlite object from which to transfer ownership of the database connection.
          */
        Sqlite(Sqlite && other) noexcept
        {
            this->database = other.database;
            other.database = nullptr;
        }

        /** Move assignment operator for the Sqlite class. It transfers ownership of the database connection from the
          * source object to the destination object.
          * @param other The source Sqlite object from which to transfer ownership of the database connection.
          */    
        Sqlite & operator = (Sqlite && other) noexcept
        {
            if (this != &other) 
            {
                this->database = other.database;
                other.database = nullptr;
            }
            return *this;
        }

    public:

        /** Executes a SQL statement that does not return results (e.g., INSERT, UPDATE, DELETE) with the specified SQL
          * code and arguments.
          * @param sql_code A string view representing the SQL statement to execute.
          * @param arguments A span of values representing the arguments to bind to the SQL statement.
          */
        void execute (string_view sql_code, std::span<const Value> arguments);

        /** Executes a SQL query that returns results (e.g., SELECT) with the specified SQL code and arguments, returning
          * a Row object to iterate over the results.
          * @param sql_code A string view representing the SQL query to execute.
          * @param arguments A span of values representing the arguments to bind to the SQL query.
          * @return A Row object that can be used to iterate over the results of the query.
          */
        Row  query (string_view sql_code, std::span<const Value> arguments);

        /** Executes a SQL statement that does not return results (e.g., INSERT, UPDATE, DELETE) with the specified SQL
          * code and a variable number of arguments.
          * @tparam ARGUMENTS The types of the arguments to bind to the SQL statement. Supported types include nullptr_t,
          *     bool, int32_t, int64_t, double, std::string, and byte_span.
          * @param sql_code A string view representing the SQL statement to execute.
          * @param arguments A variable number of values representing the arguments to bind to the SQL statement.
          */
        template<typename... ARGUMENTS>
        void execute (string_view sql_code, ARGUMENTS... arguments)
        {
            Statement * statement = prepare (sql_code);

            if (statement == nullptr)
            {
                throw_runtime_error ("Error preparing SQL statement for execution: ");
            }

            Row row(statement);

            bind_all (statement, 1, arguments...);

            while (row.advance ())
            {
            }
        }

        /** Executes a SQL query that returns results (e.g., SELECT) with the specified SQL code and a variable number
          * of arguments, returning a Row object to iterate over the results.
          * @tparam ARGUMENTS The types of the arguments to bind to the SQL query. Supported types include nullptr_t,
          *     bool, int32_t, int64_t, double, std::string, and byte_span.
          * @param sql_code A string view representing the SQL query to execute.
          * @param arguments A variable number of values representing the arguments to bind to the SQL query.
          * @return A Row object that can be used to iterate over the results of the query.
          */
        template<typename... ARGUMENTS>
        Row query (string_view sql_code, ARGUMENTS... arguments) 
        {
            Statement * statement = prepare (sql_code);

            if (statement == nullptr)
            {
                throw_runtime_error ("Error preparing SQL statement for query: ");
            }

            Row row(statement);

            bind_all (statement, 1, arguments...);

            return row;
        }

    private:

        Sqlite::Statement * prepare (string_view sql_code);

        void bind (Statement * statement, int index, nullptr_t           value);
        void bind (Statement * statement, int index, bool                value);
        void bind (Statement * statement, int index, int32_t             value);
        void bind (Statement * statement, int index, int64_t             value);
        void bind (Statement * statement, int index, double              value);
        void bind (Statement * statement, int index, byte_span           value);
        void bind (Statement * statement, int index, string_view         value);
        void bind (Statement * statement, int index, const std::string & value) { bind (statement, index, string_view(value)); }

        void bind_all (Statement * , int ) { }

        template<typename TYPE, typename... ARGUMENTS>
        void bind_all (Statement * statement, int index, TYPE value, ARGUMENTS... arguments) 
        {
            bind     (statement, index,     value       );
            bind_all (statement, index + 1, arguments...);
        }

    private:

        [[noreturn]] void throw_runtime_error (const char * message)
        {
            throw_runtime_error (message, database);
        }

        [[noreturn]] static void throw_runtime_error (const char * message, Database * database);

    };

}
