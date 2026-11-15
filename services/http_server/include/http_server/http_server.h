#pragma once

#include "common/app_config.h"
#include "common/shutdown.h"
#include "csv_store/csv_store.h"

#include <memory>

namespace collector {

// HTTP 服务封装：统一管理内部白名单与请求处理器，负责主 HTTP 服务的启动与运行。
// 职责：白名单中间件、业务路由、统一包络、异常兜底、静态页面挂载、监听线程生命周期。
// 遵循 ServiceBase 六要素语义（init/start/stop），当前仅单一子服务，按 B1 不提炼公共基类。
class HttpServer {
public:
    // 组合注入（B5）：配置为值拷贝，存储与退出控制器以引用注入（生命周期由 main 保证）。
    // 构造不做事（C6）：仅保存依赖，所有可能失败的动作在 init()。
    HttpServer(const AppOptions& opts, PointStore& store,
               ShutdownController& shutdown_ctl);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // ① init：加载内部白名单、装配响应头/中间件/路由/异常兜底/静态挂载。
    //    不监听端口、不起线程。失败返回 false 并置内部失败状态。
    bool init();

    // ② start：启动监听线程（kInitialized -> kRunning 语义）。
    //    端口绑定失败时记 ERROR、置 listen_failed 并触发 shutdown_ctl.request_stop()。
    bool start();

    // ③ stop：幂等停止——停止接受新连接、排空在途请求、join 监听线程。不抛异常。
    void stop() noexcept;

    // 监听失败查询：端口被占用等绑定失败时为 true（用于进程退出码）。
    bool listen_failed() const;

    // pimpl：公开头不引入 httplib/nlohmann 重量级库；
    // 嵌套类型置于 public 区（私有实现 src/http_server_internal.h 定义，S3 限内部使用）。
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace collector
