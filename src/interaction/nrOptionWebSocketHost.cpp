module nr.interaction;

import :protocol;
import :websocket;
import dependency.json;
import dependency.network;
import nr.options;
import nr.utils;
import std;

namespace nr::interaction
{
namespace
{
namespace json = ::dependency::json;
namespace network = dependency::network;
namespace options = nr::options;
inline constexpr std::size_t maximumSerializedEndpointRecordBytes = 1024u;

[[nodiscard]] std::optional<unsigned int> base64UrlValue(char value) noexcept
{
    if (value >= 'A' && value <= 'Z')
    {
        return static_cast<unsigned int>(value - 'A');
    }
    if (value >= 'a' && value <= 'z')
    {
        return static_cast<unsigned int>(value - 'a') + 26u;
    }
    if (value >= '0' && value <= '9')
    {
        return static_cast<unsigned int>(value - '0') + 52u;
    }
    if (value == '-')
    {
        return 62u;
    }
    if (value == '_')
    {
        return 63u;
    }
    return {};
}

[[nodiscard]] bool validBearerToken(std::string_view token) noexcept
{
    auto unpadded = token;
    if (token.size() == 44u && token.back() == '=')
    {
        unpadded.remove_suffix(1u);
    }
    if (unpadded.size() != 43u || !std::ranges::all_of(unpadded, [](char value) { return base64UrlValue(value).has_value(); }))
    {
        return false;
    }
    auto const finalValue = base64UrlValue(unpadded.back());
    return finalValue && (*finalValue & 0x03u) == 0u;
}

[[nodiscard]] std::optional<std::string> environmentBearerToken()
{
    auto const *raw = std::getenv("NR_OPTION_BEARER_TOKEN");
    return raw != nullptr ? std::optional{std::string{raw}} : std::nullopt;
}
} // namespace

class OptionWebSocketHost::Impl
{
  public:
    explicit Impl(options::OptionSystem &optionSystem) : optionSystem_(optionSystem), protocol_(optionSystem)
    {
    }

    [[nodiscard]] WebSocketHostStartResult start()
    {
        if (server_.running())
        {
            return WebSocketHostStartResult{.error = WebSocketHostStartError::alreadyRunning};
        }
        if (optionSystem_.get().authorityMode() != options::AuthorityMode::agent)
        {
            return WebSocketHostStartResult{.error = WebSocketHostStartError::wrongAuthorityMode};
        }
        auto token = environmentBearerToken();
        if (!token)
        {
            return WebSocketHostStartResult{.error = WebSocketHostStartError::missingBearerToken};
        }
        if (!validBearerToken(*token))
        {
            return WebSocketHostStartResult{.error = WebSocketHostStartError::invalidBearerToken};
        }

        auto result = server_.start(
            network::WebSocketServerConfig{.bearerToken = std::move(*token)}, [this](std::string_view payload, const network::MessageContext &context, std::string &responseSlot) { return protocol_.handleText(payload, context, responseSlot); },
            [](const network::TransportEvent &event) {
                if (event.kind == network::TransportEventKind::responseWriteFailed)
                {
                    nrLog(LogLevel::warning, "OPTION_WS", event.startedResponseLost ? "WebSocket response transport failed after a mutation may have started." : "WebSocket response transport failed.");
                }
                else if (event.kind == network::TransportEventKind::listenerError)
                {
                    nrLog(LogLevel::error, "OPTION_WS", "The loopback WebSocket listener failed.");
                }
                else if (event.kind == network::TransportEventKind::connectionClosed && event.startedResponseLost)
                {
                    nrLog(LogLevel::warning, "OPTION_WS", "WebSocket connection closed after a mutation started response was prepared but not delivered.");
                }
            });
        if (!result.started)
        {
            return WebSocketHostStartResult{
                .error = WebSocketHostStartError::networkStartFailed,
                .detail = std::move(result.detail),
            };
        }

        auto uri = result.endpoint.uri();
        auto endpointRecord = std::string{};
        auto const serializationError = json::serializeJson(
            json::JsonValue{json::JsonValue::Object{
                {"endpoint", json::JsonValue{uri}},
            }},
            endpointRecord,
            maximumSerializedEndpointRecordBytes);
        if (serializationError != json::JsonError::none)
        {
            server_.stop();
            return WebSocketHostStartResult{
                .error = WebSocketHostStartError::networkStartFailed,
                .detail = "Boost.JSON failed to serialize the option endpoint record.",
            };
        }
        nrCompactRecord<LogLevel::info>("NR_OPTION_ENDPOINT_V1", endpointRecord);
        return WebSocketHostStartResult{
            .started = true,
            .endpoint = std::move(uri),
        };
    }

    void stop() noexcept
    {
        server_.stop();
    }

    [[nodiscard]] bool running() const noexcept
    {
        return server_.running();
    }

    [[nodiscard]] std::string endpoint() const
    {
        return running() ? server_.endpoint().uri() : std::string{};
    }

  private:
    std::reference_wrapper<options::OptionSystem> optionSystem_;
    OptionRpcProtocol protocol_;
    network::LoopbackWebSocketServer server_;
};

OptionWebSocketHost::OptionWebSocketHost(options::OptionSystem &optionSystem) : impl_(std::make_unique<Impl>(optionSystem))
{
}

OptionWebSocketHost::~OptionWebSocketHost()
{
    stop();
}

WebSocketHostStartResult OptionWebSocketHost::start()
{
    return impl_->start();
}

void OptionWebSocketHost::stop() noexcept
{
    impl_->stop();
}

bool OptionWebSocketHost::running() const noexcept
{
    return impl_->running();
}

std::string OptionWebSocketHost::endpoint() const
{
    return impl_->endpoint();
}
} // namespace nr::interaction
