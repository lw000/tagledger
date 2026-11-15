#pragma once
// 优雅安全退出控制器。
//
// 设计要点（详见 docs/05-测试与安全退出规范.md 第 5 节）：
//  1. 信号处理器（signal_entry）只做"原子标志写入"，不做任何 I/O、堆分配或锁操作，
//     保证在 Windows CRT 信号线程与 POSIX 信号上下文中都是最小副作用。
//  2. install_handlers() 额外启动一个 monitor 线程轮询该标志（100ms 粒度），
//     所有真正的退出动作（回调、cv 通知）都发生在 monitor 线程或请求方线程，
//     与信号上下文解耦。
//  3. request_stop() 幂等：信号、管理端点、测试代码可重复调用，动作只触发一次。
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <mutex>

namespace collector {

class ShutdownController {
public:
    using Callback = std::function<void()>;

    ShutdownController() = default;
    ~ShutdownController();

    ShutdownController(const ShutdownController&) = delete;
    ShutdownController& operator=(const ShutdownController&) = delete;

    // 安装 SIGINT/SIGTERM（Windows 另含 SIGBREAK）处理器，并启动 monitor 线程。
    // 调用时机约定：所有组件（白名单/存储/路由）构造完成之后、开始监听之前。
    void install_handlers();

    // 请求退出（幂等，线程安全）。首次调用时触发 on_stop 回调并唤醒所有等待者。
    void request_stop();

    bool stop_requested() const { return stop_.load(std::memory_order_acquire); }

    // 阻塞等待退出请求；listen_failed 为真时（端口绑定失败）同样返回。
    void wait(bool listen_failed = false);

    // 注册首次退出请求时执行一次的回调（如通知/审计日志）。
    void set_on_stop(Callback cb);

    // 单元测试辅助：返回 monitor 线程是否已启动。
    bool monitor_running() const { return monitor_running_.load(); }

    // 返回触发退出的信号值（0 表示非信号路径，如管理端点/测试触发）。
    int last_signal() const { return last_signal_.load(); }

private:
    static void signal_entry(int sig);

    void fire_once();

    std::atomic<bool> stop_{false};
    std::atomic<bool> fired_{false};
    std::atomic<bool> monitor_running_{false};
    std::atomic<int> last_signal_{0};

    std::mutex mu_;
    std::condition_variable cv_;
    Callback on_stop_;
};
}  // namespace collector
