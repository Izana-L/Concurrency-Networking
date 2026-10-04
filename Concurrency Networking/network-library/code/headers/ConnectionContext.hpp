#pragma once
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <HttpRequestHandler.hpp>
#include <snippets.hpp>
#include <TcpListener.hpp>
#include <atomic>


namespace argb
{
    /// <summary>
    /// Se ha movido la clase ConnectionContext a su propio, para cargar menos el scritp de httpser
    /// </summary>
    struct ConnectionContext
    {
        enum State
        {
            RECEIVING_REQUEST,
            RUNNING_HANDLER,
            /////////////////////////////////////////////////////////////////////////////////////////////////////
            HANDLER_IN_PROGRESS,
            /////////////////////////////////////////////////////////////////////////////////////////////////////
            WRITING_RESPONSE_HEADER,
            WRITING_RESPONSE_BODY,
            CLOSED,
        };
        enum class LifecycleState 
        {
            IDLE,          // conexión viva, nadie la está tocando
            PROCESSING,    // el worker la está transfiriendo/ejecutando
            CLOSING,       // la limpieza ha decidido cerrarla y se encarga de ello
            CLOSED         // ya está cerrada (puede eliminarse de los vectores)
        };
        // se hace atomicas las varibles de ConnectionContext que se utilizen en varios hilos a la vez
        // las que solo se utilizan en un solo hilo se dejan igual
        std::atomic<State>       state;
        std::atomic<Timestamp>     last_activity;
        std::atomic<LifecycleState> lifecycle{ LifecycleState::IDLE };
        TcpSocket                socket;
        HttpRequest              request;
        HttpResponse             response;
        HttpRequest::Parser      request_parser;
        size_t                   response_bytes_sent;
        HttpRequestHandler::Ptr  handler;
       
        ConnectionContext();
        ConnectionContext(ConnectionContext&& other) noexcept;
        ConnectionContext(const ConnectionContext&) = delete;
        

        ConnectionContext& operator = (const ConnectionContext&) = delete;
        ConnectionContext& operator = (ConnectionContext&&) noexcept = delete;
    };
}