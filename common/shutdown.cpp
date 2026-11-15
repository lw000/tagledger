#include "common/shutdown.h"

#include <csignal>
#include <thread>

#include "common/util.h"

namespace collector {

namespace {
// 信号处理器可见的最小状态：sig_atomic_t + 无锁原子布尔。
// 处理器内只允许写这两个变量（见 docs/05 第 5.2 节）。
volatile std::sig_atomic_t g_signal_received = 0;
std::atomic<bool> g_stop_flag{false};

ShutdownController* g_controller = nullptr;
}  // namespace

void ShutdownController::signal_entry(int sig) {
    g_signal_received = sig;      // sig_atomic_t，信号安全
    g_stop_flag.store(true, std::memory_order_release);  // 无锁原子写
}

ShutdownController::~ShutdownController() {
    // monitor 线程随进程退出自动回收；进程内正常销毁时循环自然结束。
}

void ShutdownController::install_handlers() {
    g_controller = this;

    // Windows CRT 的 signal() 覆盖 CTRL_C_EVENT/SIGINT 与 CTRL_BREAK/SIGBREAK；
    // POSIX 覆盖 SIGINT/SIGTERM。SIGKILL/SIGSTOP 不可捕获（规范内明确说明）。
    std::signal(SIGINT, &signal_entry);
    std::signal(SIGTERM, &signal_entry);
#if defined(_WIN32)
    std::signal(SIGBREAK, &signal_entry);
#endif

    // monitor 线程：轮询原子标志（100ms），把退出动作从信号上下文搬到这里。
    std::thread([this]() {
        monitor_running_.store(true);
        while (!g_stop_flag.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (g_signal_received != 0)
            last_signal_.store(static_cast<int>(g_signal_received));
        {
            // 持锁写标志，避免与 wait() 的"检查-挂起"窗口竞态
            std::lock_guard<std::mutex> lk(mu_);
            stop_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
        fire_once();
    }).detach();
}

void ShutdownController::request_stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
    fire_once();
}

void ShutdownController::wait(bool listen_failed) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] {
        return stop_.load(std::memory_order_acquire) || listen_failed;
    });
}

void ShutdownController::set_on_stop(Callback cb) {
    std::lock_guard<std::mutex> lk(mu_);
    on_stop_ = std::move(cb);
}

void ShutdownController::fire_once() {
    bool expect = false;
    if (!fired_.compare_exchange_strong(expect, true)) return;
    Callback cb;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cb = on_stop_;
    }
    if (cb) cb();
}

}  // namespace collector
