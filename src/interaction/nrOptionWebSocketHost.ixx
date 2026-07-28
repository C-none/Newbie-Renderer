export module nr.interaction:websocket;

import nr.options;
import std;

export namespace nr::interaction
{
enum class WebSocketHostStartError : std::uint8_t
{
    none,
    alreadyRunning,
    wrongAuthorityMode,
    missingBearerToken,
    invalidBearerToken,
    networkStartFailed,
};

struct WebSocketHostStartResult
{
    bool started = false;
    WebSocketHostStartError error = WebSocketHostStartError::none;
    std::string endpoint{};
    std::string detail{};
};

class OptionWebSocketHost
{
  public:
    explicit OptionWebSocketHost(nr::options::OptionSystem &optionSystem);
    ~OptionWebSocketHost();

    OptionWebSocketHost(const OptionWebSocketHost &) = delete;
    OptionWebSocketHost &operator=(const OptionWebSocketHost &) = delete;
    OptionWebSocketHost(OptionWebSocketHost &&) = delete;
    OptionWebSocketHost &operator=(OptionWebSocketHost &&) = delete;

    [[nodiscard]] WebSocketHostStartResult start();
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::string endpoint() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nr::interaction
