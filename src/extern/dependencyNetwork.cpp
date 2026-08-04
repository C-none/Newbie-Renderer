module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

module dependency.network;

import std;

namespace dependency::network
{
namespace
{
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using Tcp = asio::ip::tcp;
using ErrorCode = boost::system::error_code;

[[nodiscard]] bool constantTimeEqual(std::string_view lhs, std::string_view rhs) noexcept
{
    auto difference = static_cast<unsigned int>(lhs.size() ^ rhs.size());
    auto const maximum = std::max(lhs.size(), rhs.size());
    auto indices = std::views::iota(std::size_t{0u}, maximum);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const left = index < lhs.size() ? static_cast<unsigned char>(lhs[index]) : 0u;
        auto const right = index < rhs.size() ? static_cast<unsigned char>(rhs[index]) : 0u;
        difference |= left ^ right;
    });
    return difference == 0u;
}

[[nodiscard]] std::size_t messageBufferLimit(std::size_t maximumMessageBytes) noexcept
{
    if (maximumMessageBytes == std::numeric_limits<std::size_t>::max())
    {
        return maximumMessageBytes;
    }
    return maximumMessageBytes + 1u;
}

[[nodiscard]] std::string_view handshakeReasonName(HandshakeRejectReason reason) noexcept
{
    switch (reason)
    {
    case HandshakeRejectReason::none:
        return "none";
    case HandshakeRejectReason::malformedUpgrade:
        return "malformed_upgrade";
    case HandshakeRejectReason::headersTooLarge:
        return "headers_too_large";
    case HandshakeRejectReason::targetTooLong:
        return "target_too_long";
    case HandshakeRejectReason::wrongTarget:
        return "wrong_target";
    case HandshakeRejectReason::originForbidden:
        return "origin_forbidden";
    case HandshakeRejectReason::unauthorized:
        return "unauthorized";
    case HandshakeRejectReason::controllerBusy:
        return "controller_busy";
    }
    std::unreachable();
}

[[nodiscard]] http::status handshakeStatus(HandshakeRejectReason reason) noexcept
{
    switch (reason)
    {
    case HandshakeRejectReason::headersTooLarge:
        return http::status::request_header_fields_too_large;
    case HandshakeRejectReason::targetTooLong:
        return http::status::uri_too_long;
    case HandshakeRejectReason::wrongTarget:
        return http::status::not_found;
    case HandshakeRejectReason::originForbidden:
        return http::status::forbidden;
    case HandshakeRejectReason::unauthorized:
        return http::status::unauthorized;
    case HandshakeRejectReason::controllerBusy:
        return http::status::service_unavailable;
    case HandshakeRejectReason::malformedUpgrade:
    case HandshakeRejectReason::none:
        return http::status::bad_request;
    }
    std::unreachable();
}

class TokenBucket
{
  public:
    TokenBucket(double rate, double burst)
        : rate_(rate), capacity_(burst), tokens_(burst), updated_(std::chrono::steady_clock::now())
    {
    }

    [[nodiscard]] bool consume(double amount) noexcept
    {
        refill();
        if (amount > tokens_)
        {
            return false;
        }
        tokens_ -= amount;
        return true;
    }

    [[nodiscard]] std::chrono::steady_clock::duration reserveDelay(double amount) noexcept
    {
        refill();
        if (amount <= tokens_)
        {
            tokens_ -= amount;
            return {};
        }
        auto const deficit = amount - tokens_;
        tokens_ = 0.0;
        return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>{deficit / rate_});
    }

  private:
    void refill() noexcept
    {
        auto const now = std::chrono::steady_clock::now();
        auto const elapsed = std::chrono::duration<double>{now - updated_}.count();
        tokens_ = std::min(capacity_, tokens_ + elapsed * rate_);
        updated_ = now;
    }

    double rate_;
    double capacity_;
    double tokens_;
    std::chrono::steady_clock::time_point updated_;
};

class Connection;

struct ServerState
{
    WebSocketServerConfig config{};
    TextMessageHandler messageHandler{};
    TransportEventHandler eventHandler{};
    std::weak_ptr<Connection> activeConnection{};
    std::vector<std::weak_ptr<Connection>> connections{};
    bool controllerActive = false;
    bool stopping = false;

    void emit(TransportEvent event) const
    {
        if (eventHandler)
        {
            eventHandler(event);
        }
    }
};

class Connection final : public std::enable_shared_from_this<Connection>
{
  public:
    Connection(Tcp::socket socket, std::shared_ptr<ServerState> state)
        : webSocket_(std::move(socket)), headerBuffer_(state->config.limits.maximumHeaderBytes),
          messageBuffer_(messageBufferLimit(state->config.limits.maximumMessageBytes)),
          idleTimer_(webSocket_.get_executor()), outboundTimer_(webSocket_.get_executor()), state_(std::move(state)),
          requestBucket_(state_->config.limits.requestsPerSecond, state_->config.limits.requestBurst),
          inboundBucket_(state_->config.limits.inboundBytesPerSecond, state_->config.limits.inboundBurstBytes),
          outboundBucket_(state_->config.limits.outboundBytesPerSecond, state_->config.limits.outboundBurstBytes)
    {
        responseSlot_.reserve(state_->config.limits.maximumResponseBytes);
        requestParser_.header_limit(state_->config.limits.maximumHeaderBytes);
        requestParser_.body_limit(0u);
    }

    void start()
    {
        beast::get_lowest_layer(webSocket_).expires_after(state_->config.limits.handshakeTimeout);
        http::async_read(beast::get_lowest_layer(webSocket_), headerBuffer_, requestParser_,
                         [self = shared_from_this()](ErrorCode error, std::size_t) { self->onHttpRequest(error); });
    }

    void stopFromServer() noexcept
    {
        finish("server_stopping");
    }

  private:
    void onHttpRequest(const ErrorCode &error)
    {
        if (error)
        {
            if (error == http::error::header_limit)
            {
                rejectHandshake(HandshakeRejectReason::headersTooLarge);
                return;
            }
            finish("http_read_failed");
            return;
        }

        request_.emplace(requestParser_.release());
        auto const &request = *request_;
        auto const authorizationIt = request.find(http::field::authorization);
        auto const originIt = request.find(http::field::origin);
        auto const authorization = authorizationIt != request.end() ? std::string_view{authorizationIt->value().data(),
                                                                                       authorizationIt->value().size()}
                                                                    : std::string_view{};
        auto const target = std::string_view{request.target().data(), request.target().size()};
        auto const decision = evaluateHandshake(
            HandshakeRequest{
                .target = target,
                .authorization = authorization,
                .originPresent = originIt != request.end(),
                .websocketUpgrade = websocket::is_upgrade(request),
                .controllerActive = state_->controllerActive,
            },
            state_->config.bearerToken, state_->config.limits);
        if (decision != HandshakeRejectReason::none)
        {
            rejectHandshake(decision);
            return;
        }

        state_->controllerActive = true;
        state_->activeConnection = shared_from_this();
        ownsController_ = true;

        beast::get_lowest_layer(webSocket_).expires_never();
        auto timeout = websocket::stream_base::timeout::suggested(beast::role_type::server);
        timeout.handshake_timeout = state_->config.limits.handshakeTimeout;
        timeout.idle_timeout = websocket::stream_base::none();
        timeout.keep_alive_pings = false;
        webSocket_.set_option(timeout);
        webSocket_.read_message_max(state_->config.limits.maximumMessageBytes);
        webSocket_.text(true);
        webSocket_.control_callback([weak = weak_from_this()](websocket::frame_type kind, beast::string_view) {
            if (auto self = weak.lock())
            {
                self->onControlFrame(kind);
            }
        });
        webSocket_.async_accept(
            *request_, [self = shared_from_this()](ErrorCode acceptError) { self->onWebSocketAccepted(acceptError); });
    }

    void rejectHandshake(HandshakeRejectReason reason)
    {
        state_->emit(TransportEvent{
            .kind = TransportEventKind::handshakeRejected,
            .detail = std::string{handshakeReasonName(reason)},
        });
        auto response = http::response<http::string_body>{
            handshakeStatus(reason),
            request_ ? request_->version() : 11u,
        };
        response.set(http::field::server, "Newbie-Renderer");
        response.set(http::field::content_type, "text/plain; charset=utf-8");
        if (reason == HandshakeRejectReason::unauthorized)
        {
            response.set(http::field::www_authenticate, "Bearer");
        }
        response.keep_alive(false);
        response.body() = "WebSocket upgrade rejected.";
        response.prepare_payload();
        httpResponse_.emplace(std::move(response));
        beast::get_lowest_layer(webSocket_).expires_after(state_->config.limits.handshakeTimeout);
        http::async_write(beast::get_lowest_layer(webSocket_), *httpResponse_,
                          [self = shared_from_this()](ErrorCode, std::size_t) { self->finish("handshake_rejected"); });
    }

    void onWebSocketAccepted(const ErrorCode &error)
    {
        request_.reset();
        if (error)
        {
            finish("websocket_accept_failed");
            return;
        }
        state_->emit(TransportEvent{.kind = TransportEventKind::connectionAccepted});
        lastInbound_ = std::chrono::steady_clock::now();
        scheduleIdleCheck();
        readNext();
    }

    void readNext()
    {
        if (finished_ || closing_ || responsePending_ || responseWriteInProgress_)
        {
            return;
        }
        messageBuffer_.consume(messageBuffer_.size());
        webSocket_.async_read(messageBuffer_, [self = shared_from_this()](ErrorCode error, std::size_t bytes) {
            self->onMessage(error, bytes);
        });
    }

    void onMessage(const ErrorCode &error, std::size_t bytes)
    {
        if (error)
        {
            if (error == websocket::error::closed)
            {
                finish("peer_closed");
            }
            else if (error == websocket::error::message_too_big || error == websocket::error::bad_frame_payload)
            {
                finishProtocolReadError(error == websocket::error::message_too_big ? WebSocketCloseCode::messageTooBig
                                                                                   : WebSocketCloseCode::invalidPayload,
                                        error == websocket::error::message_too_big ? "message_too_big"
                                                                                   : "invalid_utf8");
            }
            else if (error == websocket::error::buffer_overflow)
            {
                closeWith(WebSocketCloseCode::messageTooBig, "message_too_big");
            }
            else
            {
                finish("websocket_read_failed");
            }
            return;
        }

        lastInbound_ = std::chrono::steady_clock::now();
        if (!webSocket_.got_text())
        {
            closeWith(WebSocketCloseCode::unsupportedData, "binary_not_supported");
            return;
        }

        auto const payloadDecision = evaluatePayload(PayloadKind::text, true, bytes, state_->config.limits);
        if (!payloadDecision.accepted)
        {
            closeWith(*payloadDecision.closeCode, "payload_rejected");
            return;
        }

        auto const requestAllowed = requestBucket_.consume(1.0);
        auto const inboundAllowed = inboundBucket_.consume(static_cast<double>(bytes));
        auto payload = beast::buffers_to_string(messageBuffer_.data());
        responseSlot_.clear();
        auto const result =
            state_->messageHandler(std::string_view{payload},
                                   MessageContext{.rateLimited = !requestAllowed || !inboundAllowed}, responseSlot_);
        if (!result.responseReady)
        {
            readNext();
            return;
        }

        responsePending_ = true;
        startedResponsePending_ = result.mutationStarted;
        if (responseSlot_.empty() || responseSlot_.size() > state_->config.limits.maximumResponseBytes)
        {
            state_->emit(TransportEvent{
                .kind = TransportEventKind::responseWriteFailed,
                .detail = "response_slot_invalid",
                .startedResponseLost = startedResponsePending_,
            });
            startedResponsePending_ = false;
            finish("response_slot_invalid");
            return;
        }
        startResponseWhenReady();
    }

    void startResponseWhenReady()
    {
        if (!responsePending_ || responseWriteInProgress_ || controlWriteInProgress_ || finished_ || closing_)
        {
            return;
        }
        auto const delay = outboundBucket_.reserveDelay(static_cast<double>(responseSlot_.size()));
        if (delay > std::chrono::steady_clock::duration::zero())
        {
            outboundTimer_.expires_after(delay);
            outboundTimer_.async_wait([self = shared_from_this()](ErrorCode error) {
                if (!error)
                {
                    self->startResponseWrite();
                }
            });
            return;
        }
        startResponseWrite();
    }

    void startResponseWrite()
    {
        if (!responsePending_ || responseWriteInProgress_ || controlWriteInProgress_ || finished_ || closing_)
        {
            return;
        }
        responseWriteInProgress_ = true;
        webSocket_.text(true);
        webSocket_.async_write(asio::buffer(responseSlot_), [self = shared_from_this()](ErrorCode error, std::size_t) {
            self->onResponseWritten(error);
        });
    }

    void onResponseWritten(const ErrorCode &error)
    {
        responseWriteInProgress_ = false;
        if (error)
        {
            state_->emit(TransportEvent{
                .kind = TransportEventKind::responseWriteFailed,
                .detail = "response_write_failed",
                .startedResponseLost = startedResponsePending_,
            });
            startedResponsePending_ = false;
            finish("response_write_failed");
            return;
        }
        responsePending_ = false;
        startedResponsePending_ = false;
        responseSlot_.clear();
        readNext();
    }

    void onControlFrame(websocket::frame_type kind)
    {
        lastInbound_ = std::chrono::steady_clock::now();
        if (kind == websocket::frame_type::pong)
        {
            pingOutstanding_ = false;
        }
    }

    void scheduleIdleCheck()
    {
        if (finished_ || closing_)
        {
            return;
        }
        idleTimer_.expires_after(std::chrono::seconds{1});
        idleTimer_.async_wait([self = shared_from_this()](ErrorCode error) {
            if (!error)
            {
                self->checkIdle();
            }
        });
    }

    void checkIdle()
    {
        if (finished_ || closing_)
        {
            return;
        }
        auto const now = std::chrono::steady_clock::now();
        if (pingOutstanding_ && now - pingSentAt_ >= state_->config.limits.pongTimeout)
        {
            finish("pong_timeout");
            return;
        }
        if (!pingOutstanding_ && !controlWriteInProgress_ && !responsePending_ && !responseWriteInProgress_ &&
            now - lastInbound_ >= state_->config.limits.idleBeforePing)
        {
            controlWriteInProgress_ = true;
            pingSentAt_ = now;
            webSocket_.async_ping(websocket::ping_data{}, [self = shared_from_this()](ErrorCode error) {
                self->controlWriteInProgress_ = false;
                if (error)
                {
                    self->finish("ping_failed");
                    return;
                }
                self->pingOutstanding_ = true;
                self->startResponseWhenReady();
            });
        }
        scheduleIdleCheck();
    }

    void finishProtocolReadError(WebSocketCloseCode code, std::string_view detail) noexcept
    {
        state_->emit(TransportEvent{
            .kind = TransportEventKind::protocolClosed,
            .detail = std::string{detail},
            .closeCode = code,
        });
        finish("protocol_closed");
    }

    void closeWith(WebSocketCloseCode code, std::string_view detail)
    {
        if (finished_ || closing_)
        {
            return;
        }
        closing_ = true;
        static_cast<void>(idleTimer_.cancel());
        static_cast<void>(outboundTimer_.cancel());
        state_->emit(TransportEvent{
            .kind = TransportEventKind::protocolClosed,
            .detail = std::string{detail},
            .closeCode = code,
        });
        auto reason = websocket::close_reason{};
        reason.code = static_cast<websocket::close_code>(code);
        webSocket_.async_close(reason, [self = shared_from_this()](ErrorCode) { self->finish("protocol_closed"); });
    }

    void finish(std::string_view detail) noexcept
    {
        if (finished_)
        {
            return;
        }
        finished_ = true;
        static_cast<void>(idleTimer_.cancel());
        static_cast<void>(outboundTimer_.cancel());
        ErrorCode ignored;
        auto &socket = beast::get_lowest_layer(webSocket_).socket();
        socket.cancel(ignored);
        socket.shutdown(Tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
        if (ownsController_)
        {
            ownsController_ = false;
            state_->controllerActive = false;
            state_->activeConnection.reset();
        }
        state_->emit(TransportEvent{
            .kind = TransportEventKind::connectionClosed,
            .detail = std::string{detail},
            .startedResponseLost = startedResponsePending_,
        });
    }

    websocket::stream<beast::tcp_stream, false> webSocket_;
    beast::flat_buffer headerBuffer_;
    beast::flat_buffer messageBuffer_;
    http::request_parser<http::empty_body> requestParser_;
    std::optional<http::request<http::empty_body>> request_{};
    std::optional<http::response<http::string_body>> httpResponse_{};
    asio::steady_timer idleTimer_;
    asio::steady_timer outboundTimer_;
    std::shared_ptr<ServerState> state_;
    TokenBucket requestBucket_;
    TokenBucket inboundBucket_;
    TokenBucket outboundBucket_;
    std::string responseSlot_{};
    std::chrono::steady_clock::time_point lastInbound_{};
    std::chrono::steady_clock::time_point pingSentAt_{};
    bool ownsController_ = false;
    bool responsePending_ = false;
    bool responseWriteInProgress_ = false;
    bool startedResponsePending_ = false;
    bool controlWriteInProgress_ = false;
    bool pingOutstanding_ = false;
    bool closing_ = false;
    bool finished_ = false;
};

class Listener final : public std::enable_shared_from_this<Listener>
{
  public:
    Listener(asio::io_context &context, std::shared_ptr<ServerState> state)
        : context_(context), acceptor_(context), state_(std::move(state))
    {
    }

    [[nodiscard]] ServerStartResult open()
    {
        auto error = ErrorCode{};
        auto const endpoint = Tcp::endpoint{asio::ip::address_v4::loopback(), 0u};
        acceptor_.open(Tcp::v4(), error);
        if (error)
        {
            return failure(ServerStartError::listenerOpenFailed, error);
        }
        acceptor_.set_option(asio::socket_base::reuse_address{false}, error);
        if (error)
        {
            return failure(ServerStartError::listenerOpenFailed, error);
        }
        acceptor_.bind(endpoint, error);
        if (error)
        {
            return failure(ServerStartError::listenerBindFailed, error);
        }
        acceptor_.listen(1, error);
        if (error)
        {
            return failure(ServerStartError::listenerListenFailed, error);
        }
        auto const local = acceptor_.local_endpoint(error);
        if (error)
        {
            return failure(ServerStartError::listenerBindFailed, error);
        }
        endpoint_ = WebSocketEndpoint{.port = local.port()};
        return ServerStartResult{
            .started = true,
            .endpoint = endpoint_,
        };
    }

    void startAccepting()
    {
        acceptor_.async_accept(asio::make_strand(context_.get()), [self = shared_from_this()](ErrorCode error,
                                                                                              Tcp::socket socket) {
            if (!error)
            {
                std::erase_if(self->state_->connections, [](auto const &connection) { return connection.expired(); });
                auto connection = std::make_shared<Connection>(std::move(socket), self->state_);
                self->state_->connections.emplace_back(connection);
                connection->start();
            }
            else if (!self->state_->stopping)
            {
                self->state_->emit(TransportEvent{
                    .kind = TransportEventKind::listenerError,
                    .detail = "accept_failed",
                });
            }
            if (!self->state_->stopping)
            {
                self->startAccepting();
            }
        });
    }

    void stop() noexcept
    {
        state_->stopping = true;
        auto error = ErrorCode{};
        acceptor_.cancel(error);
        acceptor_.close(error);
        if (auto active = state_->activeConnection.lock())
        {
            active->stopFromServer();
        }
        std::ranges::for_each(state_->connections, [](auto const &weak) {
            if (auto connection = weak.lock())
            {
                connection->stopFromServer();
            }
        });
    }

    [[nodiscard]] WebSocketEndpoint endpoint() const
    {
        return endpoint_;
    }

  private:
    [[nodiscard]] static ServerStartResult failure(ServerStartError kind, const ErrorCode &error)
    {
        return ServerStartResult{
            .error = kind,
            .detail = error.message(),
        };
    }

    std::reference_wrapper<asio::io_context> context_;
    Tcp::acceptor acceptor_;
    std::shared_ptr<ServerState> state_;
    WebSocketEndpoint endpoint_{};
};
} // namespace

HandshakeRejectReason evaluateHandshake(const HandshakeRequest &request, std::string_view bearerToken,
                                        const WebSocketLimits &limits) noexcept
{
    if (!request.websocketUpgrade)
    {
        return HandshakeRejectReason::malformedUpgrade;
    }
    if (request.headerBytes > limits.maximumHeaderBytes)
    {
        return HandshakeRejectReason::headersTooLarge;
    }
    if (request.target.size() > limits.maximumTargetBytes)
    {
        return HandshakeRejectReason::targetTooLong;
    }
    if (request.target != optionWebSocketTarget)
    {
        return HandshakeRejectReason::wrongTarget;
    }
    if (request.originPresent)
    {
        return HandshakeRejectReason::originForbidden;
    }
    auto expected = std::string{"Bearer "};
    expected += bearerToken;
    if (bearerToken.empty() || !constantTimeEqual(request.authorization, expected))
    {
        return HandshakeRejectReason::unauthorized;
    }
    if (request.controllerActive)
    {
        return HandshakeRejectReason::controllerBusy;
    }
    return HandshakeRejectReason::none;
}

PayloadDecision evaluatePayload(PayloadKind kind, bool validUtf8, std::size_t reassembledBytes,
                                const WebSocketLimits &limits) noexcept
{
    if (kind == PayloadKind::binary)
    {
        return PayloadDecision{.closeCode = WebSocketCloseCode::unsupportedData};
    }
    if (!validUtf8)
    {
        return PayloadDecision{.closeCode = WebSocketCloseCode::invalidPayload};
    }
    if (reassembledBytes > limits.maximumMessageBytes)
    {
        return PayloadDecision{.closeCode = WebSocketCloseCode::messageTooBig};
    }
    return PayloadDecision{.accepted = true};
}

std::string WebSocketEndpoint::uri() const
{
    return std::format("ws://{}:{}{}", address, port, target);
}

class LoopbackWebSocketServer::Impl
{
  public:
    [[nodiscard]] ServerStartResult start(WebSocketServerConfig config, TextMessageHandler messageHandler,
                                          TransportEventHandler eventHandler)
    {
        if (running_.load(std::memory_order_acquire))
        {
            return ServerStartResult{.error = ServerStartError::alreadyRunning};
        }
        if (config.bearerToken.empty() || !messageHandler || config.limits.maximumMessageBytes == 0u ||
            config.limits.maximumResponseBytes == 0u || config.limits.requestsPerSecond <= 0.0 ||
            config.limits.requestBurst <= 0.0 || config.limits.inboundBytesPerSecond <= 0.0 ||
            config.limits.inboundBurstBytes <= 0.0 || config.limits.outboundBytesPerSecond <= 0.0 ||
            config.limits.outboundBurstBytes < static_cast<double>(config.limits.maximumResponseBytes))
        {
            return ServerStartResult{.error = ServerStartError::invalidConfiguration};
        }

        context_.restart();
        state_ = std::make_shared<ServerState>(ServerState{
            .config = std::move(config),
            .messageHandler = std::move(messageHandler),
            .eventHandler = std::move(eventHandler),
        });
        listener_ = std::make_shared<Listener>(context_, state_);
        auto result = listener_->open();
        if (!result.started)
        {
            listener_.reset();
            state_.reset();
            return result;
        }
        endpoint_ = result.endpoint;
        workGuard_.emplace(context_.get_executor());
        listener_->startAccepting();
        running_.store(true, std::memory_order_release);
        ioThread_ = std::jthread{[this] { context_.run(); }};
        return result;
    }

    void stop() noexcept
    {
        if (!running_.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        if (listener_)
        {
            auto completion = std::make_shared<std::promise<void>>();
            auto future = completion->get_future();
            asio::post(context_, [listener = listener_, completion] {
                listener->stop();
                completion->set_value();
            });
            (void)future.wait_for(std::chrono::seconds{5});
        }
        workGuard_.reset();
        context_.stop();
        if (ioThread_.joinable())
        {
            ioThread_.join();
        }
        context_.restart();
        (void)context_.poll();
        context_.stop();
        listener_.reset();
        state_.reset();
        endpoint_ = {};
    }

    [[nodiscard]] bool running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] WebSocketEndpoint endpoint() const
    {
        return endpoint_;
    }

  private:
    asio::io_context context_{1};
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> workGuard_{};
    std::shared_ptr<ServerState> state_{};
    std::shared_ptr<Listener> listener_{};
    std::jthread ioThread_{};
    std::atomic_bool running_ = false;
    WebSocketEndpoint endpoint_{};
};

LoopbackWebSocketServer::LoopbackWebSocketServer() : impl_(std::make_unique<Impl>())
{
}

LoopbackWebSocketServer::~LoopbackWebSocketServer()
{
    stop();
}

ServerStartResult LoopbackWebSocketServer::start(WebSocketServerConfig config, TextMessageHandler messageHandler,
                                                 TransportEventHandler eventHandler)
{
    return impl_->start(std::move(config), std::move(messageHandler), std::move(eventHandler));
}

void LoopbackWebSocketServer::stop() noexcept
{
    impl_->stop();
}

bool LoopbackWebSocketServer::running() const noexcept
{
    return impl_->running();
}

WebSocketEndpoint LoopbackWebSocketServer::endpoint() const
{
    return impl_->endpoint();
}
} // namespace dependency::network
