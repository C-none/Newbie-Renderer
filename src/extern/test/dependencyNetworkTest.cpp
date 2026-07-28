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

module dependency.network.test;

import dependency.network;
import std;

namespace dependency::network::test
{
namespace
{
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using Tcp = asio::ip::tcp;
using ErrorCode = boost::system::error_code;

[[nodiscard]] std::optional<std::uint16_t> closeCode(const websocket::stream<beast::tcp_stream, false> &stream, const ErrorCode &error) noexcept
{
    if (error != websocket::error::closed)
    {
        return {};
    }
    return static_cast<std::uint16_t>(stream.reason().code);
}
} // namespace

class WebSocketClient::Impl
{
  public:
    Impl() : webSocket_(context_)
    {
    }

    ~Impl()
    {
        close();
    }

    [[nodiscard]] ConnectResult connect(const WebSocketEndpoint &endpoint, std::string bearerToken, std::optional<std::string> origin)
    {
        if (connectAttempted_)
        {
            return ConnectResult{.detail = "A test WebSocket client can perform only one connection attempt."};
        }
        connectAttempted_ = true;
        if (endpoint.port == 0u || endpoint.address.empty() || endpoint.target.empty() || bearerToken.empty())
        {
            return ConnectResult{.detail = "The test WebSocket endpoint or bearer token is invalid."};
        }

        auto error = ErrorCode{};
        auto const address = asio::ip::make_address(endpoint.address, error);
        if (error)
        {
            return ConnectResult{.detail = error.message()};
        }

        beast::get_lowest_layer(webSocket_).expires_after(std::chrono::seconds{5});
        beast::get_lowest_layer(webSocket_).connect(Tcp::endpoint{address, endpoint.port}, error);
        if (error)
        {
            return ConnectResult{.detail = error.message()};
        }

        auto timeout = websocket::stream_base::timeout::suggested(beast::role_type::client);
        timeout.handshake_timeout = std::chrono::seconds{5};
        webSocket_.set_option(timeout);
        webSocket_.auto_fragment(false);

        auto authorization = std::format("Bearer {}", bearerToken);
        webSocket_.set_option(websocket::stream_base::decorator([authorization = std::move(authorization), origin = std::move(origin)](websocket::request_type &request) {
            request.set(http::field::authorization, authorization);
            if (origin)
            {
                request.set(http::field::origin, *origin);
            }
        }));

        auto response = websocket::response_type{};
        auto const host = std::format("{}:{}", endpoint.address, endpoint.port);
        webSocket_.handshake(response, host, endpoint.target, error);
        if (error)
        {
            auto result = ConnectResult{
                .httpStatus = static_cast<std::uint16_t>(response.result_int()),
                .detail = error.message(),
            };
            closeSocket();
            return result;
        }

        beast::get_lowest_layer(webSocket_).expires_never();
        connected_ = true;
        return ConnectResult{
            .connected = true,
            .httpStatus = static_cast<std::uint16_t>(response.result_int()),
        };
    }

    [[nodiscard]] WriteResult writeText(std::string_view payload)
    {
        if (!connected_)
        {
            return WriteResult{.detail = "The test WebSocket client is not connected."};
        }
        webSocket_.text(true);
        auto error = ErrorCode{};
        static_cast<void>(webSocket_.write(asio::buffer(payload.data(), payload.size()), error));
        return writeResult(error);
    }

    [[nodiscard]] WriteResult writeTextFragments(std::span<const std::string_view> fragments)
    {
        if (!connected_)
        {
            return WriteResult{.detail = "The test WebSocket client is not connected."};
        }
        if (fragments.size() < 2u || std::ranges::any_of(fragments, [](std::string_view value) { return value.empty(); }))
        {
            return WriteResult{.detail = "A fragmented test message requires at least two non-empty fragments."};
        }

        webSocket_.text(true);
        auto error = ErrorCode{};
        auto indices = std::views::iota(std::size_t{0u}, fragments.size());
        auto written = true;
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (!written)
            {
                return;
            }
            auto const fragment = fragments[index];
            auto const finalFragment = index + 1u == fragments.size();
            static_cast<void>(webSocket_.write_some(finalFragment, asio::buffer(fragment.data(), fragment.size()), error));
            written = !error;
        });
        return writeResult(error);
    }

    [[nodiscard]] WriteResult writeBinary(std::string_view payload)
    {
        if (!connected_)
        {
            return WriteResult{.detail = "The test WebSocket client is not connected."};
        }
        webSocket_.binary(true);
        auto error = ErrorCode{};
        static_cast<void>(webSocket_.write(asio::buffer(payload.data(), payload.size()), error));
        return writeResult(error);
    }

    [[nodiscard]] ReadResult read()
    {
        if (!connected_)
        {
            return ReadResult{.detail = "The test WebSocket client is not connected."};
        }

        auto buffer = beast::flat_buffer{};
        auto error = ErrorCode{};
        static_cast<void>(webSocket_.read(buffer, error));
        if (error)
        {
            auto result = ReadResult{
                .closeCode = closeCode(webSocket_, error),
                .detail = error.message(),
            };
            connected_ = false;
            closeSocket();
            return result;
        }
        return ReadResult{
            .messageReceived = true,
            .text = beast::buffers_to_string(buffer.data()),
        };
    }

    void abort() noexcept
    {
        connected_ = false;
        auto error = ErrorCode{};
        auto &socket = beast::get_lowest_layer(webSocket_).socket();
        socket.set_option(asio::socket_base::linger{true, 0}, error);
        socket.cancel(error);
        socket.close(error);
    }

    void close() noexcept
    {
        if (connected_)
        {
            auto error = ErrorCode{};
            webSocket_.close(websocket::close_code::normal, error);
            connected_ = false;
        }
        closeSocket();
    }

    [[nodiscard]] bool connected() const noexcept
    {
        return connected_;
    }

  private:
    [[nodiscard]] WriteResult writeResult(const ErrorCode &error)
    {
        if (!error)
        {
            return WriteResult{.written = true};
        }
        auto result = WriteResult{
            .closeCode = closeCode(webSocket_, error),
            .detail = error.message(),
        };
        connected_ = false;
        closeSocket();
        return result;
    }

    void closeSocket() noexcept
    {
        auto error = ErrorCode{};
        auto &socket = beast::get_lowest_layer(webSocket_).socket();
        socket.cancel(error);
        socket.shutdown(Tcp::socket::shutdown_both, error);
        socket.close(error);
    }

    asio::io_context context_{1};
    websocket::stream<beast::tcp_stream, false> webSocket_;
    bool connectAttempted_ = false;
    bool connected_ = false;
};

WebSocketClient::WebSocketClient() : impl_(std::make_unique<Impl>())
{
}

WebSocketClient::~WebSocketClient() = default;

ConnectResult WebSocketClient::connect(const WebSocketEndpoint &endpoint, std::string bearerToken, std::optional<std::string> origin)
{
    return impl_->connect(endpoint, std::move(bearerToken), std::move(origin));
}

WriteResult WebSocketClient::writeText(std::string_view payload)
{
    return impl_->writeText(payload);
}

WriteResult WebSocketClient::writeTextFragments(std::span<const std::string_view> fragments)
{
    return impl_->writeTextFragments(fragments);
}

WriteResult WebSocketClient::writeBinary(std::string_view payload)
{
    return impl_->writeBinary(payload);
}

ReadResult WebSocketClient::read()
{
    return impl_->read();
}

void WebSocketClient::abort() noexcept
{
    impl_->abort();
}

void WebSocketClient::close() noexcept
{
    impl_->close();
}

bool WebSocketClient::connected() const noexcept
{
    return impl_->connected();
}
} // namespace dependency::network::test
