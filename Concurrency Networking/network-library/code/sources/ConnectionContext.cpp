#include <ConnectionContext.hpp>
#include <utility> // para std::move
namespace argb
{
    ConnectionContext::ConnectionContext()
        : state(RECEIVING_REQUEST)
        , last_activity(now())
        , request_parser(request)
        , response_bytes_sent(0)
    {
    }

    ConnectionContext::ConnectionContext(ConnectionContext&& other) noexcept
        : state(other.state.load())
        , last_activity(other.last_activity.load())
        , socket(std::move(other.socket))
        , request(std::move(other.request))
        , response(std::move(other.response))
        , request_parser(request)          // se re-inicializa con la request de este nuevo objeto
        , handler(std::move(other.handler))
        , response_bytes_sent(other.response_bytes_sent)
    {
    }
}