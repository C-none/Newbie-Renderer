export module dependency.network;

import std;

export namespace dependency::network
{
struct WebSocketLimits
{
    std::size_t maximumMessageBytes = 256u * 1024u;
    std::size_t maximumResponseBytes = 256u * 1024u;
    std::size_t maximumHeaderBytes = 8u * 1024u;
    std::size_t maximumTargetBytes = 256u;
    double requestsPerSecond = 240.0;
    double requestBurst = 60.0;
    double inboundBytesPerSecond = 4.0 * 1024.0 * 1024.0;
    double inboundBurstBytes = 512.0 * 1024.0;
    double outboundBytesPerSecond = 16.0 * 1024.0 * 1024.0;
    double outboundBurstBytes = 1024.0 * 1024.0;
    std::chrono::seconds handshakeTimeout{5};
    std::chrono::seconds idleBeforePing{15};
    std::chrono::seconds pongTimeout{45};
};

inline constexpr std::string_view optionWebSocketTarget = "/v1/options";

struct HandshakeRequest
{
    std::string_view target{};
    std::string_view authorization{};
    std::size_t headerBytes = 0u;
    bool originPresent = false;
    bool websocketUpgrade = true;
    bool controllerActive = false;
};

enum class HandshakeRejectReason : std::uint8_t
{
    none,
    malformedUpgrade,
    headersTooLarge,
    targetTooLong,
    wrongTarget,
    originForbidden,
    unauthorized,
    controllerBusy,
};

[[nodiscard]] HandshakeRejectReason evaluateHandshake(const HandshakeRequest &request, std::string_view bearerToken, const WebSocketLimits &limits = {}) noexcept;

enum class WebSocketCloseCode : std::uint16_t
{
    unsupportedData = 1003u,
    invalidPayload = 1007u,
    messageTooBig = 1009u,
};

enum class PayloadKind : std::uint8_t
{
    text,
    binary,
};

struct PayloadDecision
{
    bool accepted = false;
    std::optional<WebSocketCloseCode> closeCode{};
};

[[nodiscard]] PayloadDecision evaluatePayload(PayloadKind kind, bool validUtf8, std::size_t reassembledBytes, const WebSocketLimits &limits = {}) noexcept;

struct MessageContext
{
    bool rateLimited = false;
};

struct TextMessageResult
{
    bool responseReady = false;
    bool mutationStarted = false;
};

enum class TransportEventKind : std::uint8_t
{
    connectionAccepted,
    connectionClosed,
    handshakeRejected,
    responseWriteFailed,
    protocolClosed,
    listenerError,
};

struct TransportEvent
{
    TransportEventKind kind = TransportEventKind::connectionClosed;
    std::string detail{};
    bool startedResponseLost = false;
    std::optional<WebSocketCloseCode> closeCode{};
};

using TextMessageHandler = std::function<TextMessageResult(std::string_view, const MessageContext &, std::string &)>;
using TransportEventHandler = std::function<void(const TransportEvent &)>;

struct WebSocketEndpoint
{
    std::string address = "127.0.0.1";
    std::uint16_t port = 0u;
    std::string target = std::string{optionWebSocketTarget};

    [[nodiscard]] std::string uri() const;
};

enum class ServerStartError : std::uint8_t
{
    none,
    alreadyRunning,
    invalidConfiguration,
    listenerOpenFailed,
    listenerBindFailed,
    listenerListenFailed,
};

struct ServerStartResult
{
    bool started = false;
    ServerStartError error = ServerStartError::none;
    WebSocketEndpoint endpoint{};
    std::string detail{};
};

struct WebSocketServerConfig
{
    std::string bearerToken{};
    WebSocketLimits limits{};
};

class LoopbackWebSocketServer
{
  public:
    LoopbackWebSocketServer();
    ~LoopbackWebSocketServer();

    LoopbackWebSocketServer(const LoopbackWebSocketServer &) = delete;
    LoopbackWebSocketServer &operator=(const LoopbackWebSocketServer &) = delete;
    LoopbackWebSocketServer(LoopbackWebSocketServer &&) = delete;
    LoopbackWebSocketServer &operator=(LoopbackWebSocketServer &&) = delete;

    [[nodiscard]] ServerStartResult start(WebSocketServerConfig config, TextMessageHandler messageHandler, TransportEventHandler eventHandler = {});
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] WebSocketEndpoint endpoint() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dependency::network
