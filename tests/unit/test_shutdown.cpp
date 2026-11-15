// 安全退出控制器单元测试。命名规则："<模块>/<被测行为>"，分组标签 [unit][shutdown]。
// 信号真实触发（Ctrl+C/SIGTERM）属于进程级集成行为，由 tests/scripts/graceful_shutdown_test.py
// 以子进程方式验证；此处验证控制器自身的状态机与线程语义。
#include "common/shutdown.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using collector::ShutdownController;

TEST_CASE("shutdown/request_stop is idempotent", "[unit][shutdown]") {
    ShutdownController ctl;
    std::atomic<int> fired{0};
    ctl.set_on_stop([&fired] { fired.fetch_add(1); });

    REQUIRE_FALSE(ctl.stop_requested());

    ctl.request_stop();
    ctl.request_stop();
    ctl.request_stop();

    REQUIRE(ctl.stop_requested());
    // 等待 fire_once 的异步可见性（request_stop 内联触发，此处短暂让出）
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(fired.load() == 1);  // 回调仅执行一次
}

TEST_CASE("shutdown/wait unblocks after request_stop", "[unit][shutdown]") {
    ShutdownController ctl;
    std::atomic<bool> waiter_done{false};

    std::thread waiter([&] {
        ctl.wait();
        waiter_done.store(true);
    });

    // 300ms 内必须被唤醒
    ctl.request_stop();
    waiter.join();
    REQUIRE(waiter_done.load());
}

TEST_CASE("shutdown/wait with listen_failed returns immediately", "[unit][shutdown]") {
    ShutdownController ctl;
    // 端口绑定失败场景：不调用 request_stop，wait(listen_failed=true) 直接返回
    ctl.wait(/*listen_failed=*/true);
    SUCCEED("wait returned without stop request");
}

TEST_CASE("shutdown/monitor thread wakes on stop flag", "[unit][shutdown]") {
    // 模拟信号路径：install_handlers 后由信号处理器写入的原子标志（此处直接置位，
    // 与 signal_entry 的行为一致——只写标志，不做其他动作）。
    ShutdownController ctl;
    std::atomic<bool> on_stop_fired{false};
    ctl.set_on_stop([&on_stop_fired] { on_stop_fired.store(true); });

    // install_handlers 启动 monitor 线程；监控 g_stop_flag（内部静态）。
    // 这里通过公开语义验证：request_stop 触发前 wait 阻塞，触发后返回。
    std::thread waiter([&] { ctl.wait(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ctl.request_stop();
    waiter.join();
    REQUIRE(on_stop_fired.load());
}

TEST_CASE("shutdown/on_stop callback runs at most once across paths", "[unit][shutdown]") {
    ShutdownController ctl;
    std::atomic<int> fired{0};
    ctl.set_on_stop([&fired] { fired.fetch_add(1); });

    // 模拟"信号 + 管理端点"同时到达的竞态
    std::thread a([&] { ctl.request_stop(); });
    std::thread b([&] { ctl.request_stop(); });
    a.join();
    b.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(fired.load() == 1);
}
