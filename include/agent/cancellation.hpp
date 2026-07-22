#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>

namespace swe_agent::agent {

/** @brief 阻塞操作因 StopToken 被请求而提前终止。 */
class OperationCancelled : public std::runtime_error {
public:
    OperationCancelled() : std::runtime_error{"Operation cancelled"} {}
};

class StopSource;

// 轻量协作式停止令牌。复制后共享同一个原子状态，不负责中断阻塞操作。
// 自定义实现用于兼容当前工具链缺失的 std::stop_token/std::jthread。
class StopToken {
public:
    StopToken() = default;

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ && state_->load();
    }

private:
    friend class StopSource;

    explicit StopToken(std::shared_ptr<std::atomic_bool> state)
        : state_(std::move(state)) {}

    std::shared_ptr<std::atomic_bool> state_;
};

class StopSource {
public:
    StopSource()
        : state_(std::make_shared<std::atomic_bool>(false)) {}

    [[nodiscard]] StopToken token() const noexcept {
        return StopToken{state_};
    }

    // 请求不可撤销；所有已复制的 StopToken 都会观察到该状态。
    void request_stop() noexcept {
        state_->store(true);
    }

private:
    std::shared_ptr<std::atomic_bool> state_;
};

}  // namespace swe_agent::agent
