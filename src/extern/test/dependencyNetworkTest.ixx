export module dependency.network.test;

import dependency.network;
import std;

export namespace dependency::network::test
{
struct ConnectResult
{
    bool connected = false;
    std::uint16_t httpStatus = 0u;
    std::string detail{};
};

struct WriteResult
{
    bool written = false;
    std::optional<std::uint16_t> closeCode{};
    std::string detail{};
};

struct ReadResult
{
    bool messageReceived = false;
    std::string text{};
    std::optional<std::uint16_t> closeCode{};
    std::string detail{};
};

class WebSocketClient
{
  public:
    WebSocketClient();
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient &) = delete;
    WebSocketClient &operator=(const WebSocketClient &) = delete;
    WebSocketClient(WebSocketClient &&) = delete;
    WebSocketClient &operator=(WebSocketClient &&) = delete;

    [[nodiscard]] ConnectResult connect(const WebSocketEndpoint &endpoint, std::string bearerToken, std::optional<std::string> origin = {});
    [[nodiscard]] WriteResult writeText(std::string_view payload);
    [[nodiscard]] WriteResult writeTextFragments(std::span<const std::string_view> fragments);
    [[nodiscard]] WriteResult writeBinary(std::string_view payload);
    [[nodiscard]] ReadResult read();
    void abort() noexcept;
    void close() noexcept;
    [[nodiscard]] bool connected() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dependency::network::test
