
-- ---------------------------------------------------------------------------
-- Setup: create and seed the test table if it does not exist yet.
-- This runs once when the script is loaded.
-- ---------------------------------------------------------------------------

database.execute ([[
    CREATE TABLE IF NOT EXISTS users (
        id    INTEGER PRIMARY KEY AUTOINCREMENT,
        name  TEXT    NOT NULL,
        email TEXT    NOT NULL UNIQUE,
        score REAL    NOT NULL DEFAULT 0
    )
]])

database.execute ("DELETE FROM users")

database.execute ("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {"Alice", "alice@example.com", 95.5})
database.execute ("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {"Bob",   "bob@example.com",   80.0})
database.execute ("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {"Eve",   "eve@example.com",   73.2})

-- ---------------------------------------------------------------------------
-- GET /hello
-- Tests: request:get_method(), request:get_header(), response pipeline
-- ---------------------------------------------------------------------------

server.route ("GET", "/hello", function (request, response)

    coroutine.yield()

    local message = "Hello from the bridge! Method: " .. request:get_method()
                 .. " | Agent: " .. (request:get_header("User-Agent") or "unknown")
    coroutine.yield()

    response:status     (200)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  #message)
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (message)

end)

-- ---------------------------------------------------------------------------
-- GET /users
-- Tests: database.query with no bind args, row:advance(), row:get_integer(),
--        row:get_string(), row:get_real()
-- ---------------------------------------------------------------------------

server.route ("GET", "/users", function (request, response)

    coroutine.yield()

    local body = ""
    local row  = database.query("SELECT id, name, email, score FROM users ORDER BY id")

    coroutine.yield()

    while row:advance() do
        local id    = row:get_integer (1)
        local name  = row:get_string  (2)
        local email = row:get_string  (3)
        local score = row:get_real    (4)

        body = body .. id .. " | " .. name .. " | " .. email .. " | " .. score .. "\n"
        coroutine.yield()
    end

    response:status     (200)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  #body)
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (body)

end)

-- ---------------------------------------------------------------------------
-- GET /users/<id>
-- Tests: request:get_path(), database.query with an integer bind arg, empty result set
-- ---------------------------------------------------------------------------

server.route ("GET", "/users/", function (request, response)

    coroutine.yield()

    local path = request:get_path ()
    local id   = tonumber (path:match ("/users/(%d+)"))

    if id == nil then
        local message = "Bad request: expected /users/<integer id>"
        coroutine.yield()
        response:status     (400)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  #message)
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
        return
    end

    local row = database.query("SELECT name, email, score FROM users WHERE id = ?", {id})
    coroutine.yield()

    if row:advance() then
        local body = "name="   .. row:get_string (1)
                  .. " email=" .. row:get_string (2)
                  .. " score=" .. row:get_real   (3)

        response:status     (200)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  #body)
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (body)
    else
        local message = "User " .. id .. " not found"
        coroutine.yield()
        response:status     (404)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  #message)
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
    end

end)

-- ---------------------------------------------------------------------------
-- POST /users
-- Tests: request:get_body(), database.execute with multiple bind args,
--        database.query with a string bind arg
-- ---------------------------------------------------------------------------

server.route ("POST", "/users", function (request, response)

    coroutine.yield()

    local body  = request:get_body ()
    local name  = body:match ("name=([^&]+)")
    local email = body:match ("email=([^&]+)")
    local score = tonumber (body:match ("score=([^&]+)"))

    if name == nil or email == nil or score == nil then
        local message = "Bad request: expected body 'name=...&email=...&score=...'"
        coroutine.yield()
        response:status     (400)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  #message)
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
        return
    end

    database.execute("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {name, email, score})
    
    coroutine.yield()

    local row     = database.query("SELECT id FROM users WHERE email = ?", {email})

    coroutine.yield()

    local message

    if row:advance() then
        message = "Created user with id=" .. row:get_integer(1)
    else
        message = "Insert succeeded but could not retrieve new id"
    end

    response:status     (201)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  #message)
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (message)

end)

-- ---------------------------------------------------------------------------
-- DELETE /users/<id>
-- Tests: database.execute with an integer bind arg
-- ---------------------------------------------------------------------------

server.route ("DELETE", "/users/", function (request, response)

    coroutine.yield()

    local path = request:get_path ()
    local id   = tonumber (path:match ("/users/(%d+)"))

    if id == nil then
        local message = "Bad request: expected /users/<integer id>"
        coroutine.yield()
        response:status     (400)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  #message)
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
        return
    end

    database.execute("DELETE FROM users WHERE id = ?", {id})
    coroutine.yield()

    local message = "Deleted user " .. id

    response:status     (200)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  #message)
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (message)

end)

