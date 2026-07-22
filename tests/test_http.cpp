#include <catch2/catch_test_macros.hpp>

#include "agent/cancellation.hpp"
#include "http/http_client.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

class StallingHttpServer {
public:
    StallingHttpServer() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) {
            throw std::runtime_error{"Unable to create test socket"};
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(
                listener_,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) != 0 ||
            ::listen(listener_, 1) != 0) {
            ::close(listener_);
            throw std::runtime_error{"Unable to bind test socket"};
        }

        socklen_t length = sizeof(address);
        if (::getsockname(
                listener_,
                reinterpret_cast<sockaddr*>(&address),
                &length) != 0) {
            ::close(listener_);
            throw std::runtime_error{"Unable to inspect test socket"};
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~StallingHttpServer() {
        {
            std::lock_guard lock{mutex_};
            released_ = true;
        }
        condition_.notify_all();
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    [[nodiscard]] std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/stall";
    }

private:
    void serve() {
        const int client = ::accept(listener_, nullptr, nullptr);
        if (client < 0) {
            return;
        }
        char buffer[1024];
        (void)::read(client, buffer, sizeof(buffer));
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [this] { return released_; });
        lock.unlock();
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
    }

    int listener_{-1};
    std::uint16_t port_{0};
    std::mutex mutex_;
    std::condition_variable condition_;
    bool released_{false};
    std::thread worker_;
};

}  // namespace

TEST_CASE("HTTP request is interrupted by stop token", "[http][cancel]") {
    StallingHttpServer server;
    swe_agent::agent::StopSource stop_source;
    std::thread stopper([&stop_source] {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        stop_source.request_stop();
    });
    swe_agent::http::HttpClient client;
    const auto started = std::chrono::steady_clock::now();

    REQUIRE_THROWS_AS(
        client.post(
            server.url(),
            {"Content-Type: application/json"},
            "{}",
            stop_source.token()),
        swe_agent::agent::OperationCancelled);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    stopper.join();

    REQUIRE(elapsed < std::chrono::seconds{3});
}
