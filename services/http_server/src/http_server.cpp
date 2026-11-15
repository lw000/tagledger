#include "http_server_internal.h"

#include <utility>

#include "common/logger.h"
#include "common/util.h"

namespace collector {

using nlohmann::json;

HttpServer::HttpServer(const AppOptions& opts, PointStore& store,
                       ShutdownController& shutdown_ctl)
    : impl_(new Impl(opts, store, shutdown_ctl)) {}

HttpServer::~HttpServer() {
    stop();  // 兜底：即使 main 未显式 stop 也保证资源释放（幂等）
}

bool HttpServer::init() {
    if (impl_->running.load()) return false;  // 已初始化/运行中，拒绝重复

    // ---- 内部白名单加载（由 HttpServer 统一管理）----
    // whitelist.conf 缺失时进入开放模式（load 内部记 WARN），行为与原实现一致。
    impl_->whitelist = Whitelist::load(
        path_join(impl_->opts.config_dir, "whitelist.conf"));

    auto& svr = impl_->svr;

    // ---------- ⓪ 全局响应头：所有响应（API 与静态页）禁用缓存，
    // 防止浏览器/代理使用陈旧的点表数据或旧版页面（与前端 GET _ts 参数双保险） ----------
    svr.set_default_headers({{"Cache-Control", "no-store"}});

    // ---------- ① 白名单中间件（内部白名单） ----------
    svr.set_pre_routing_handler(
        [this](const httplib::Request& req, httplib::Response& res) {
            const std::string ip = req.remote_addr;
            const std::string origin =
                req.has_header("Origin") ? req.get_header_value("Origin") : "";
            if (impl_->whitelist.allowed(ip, origin))
                return httplib::Server::HandlerResponse::Unhandled;

            json details = {{"ip", ip}, {"origin", origin}};
            send_error(res, 403, 40301, "access denied by whitelist",
                       std::move(details));
            log_warning(
                     "whitelist DENY " + ip + " origin=" +
                         (origin.empty() ? "-" : origin) + " " + req.method +
                         " " + req.path);
            return httplib::Server::HandlerResponse::Handled;
        });

    // ---------- ② 统一异常兜底 ----------
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                 std::exception_ptr ep) {
        std::string what = "unknown exception";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        log_error("unhandled exception: " + what);
        send_error(res, 500, 50001, "internal server error");
    });

    // ---------- ③ 业务路由（请求处理器） ----------
    setup_routes(*impl_);

    // ---------- ④ 未知 /api 路径/方法 → 统一 JSON 错误 ----------
    // 注意：httplib 会对所有 status>=400 的响应回调本 handler，业务 handler 已写体时不可覆盖。
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (req.path.rfind("/api", 0) == 0) {
            if (!res.body.empty()) return;  // 业务 handler 已产生错误体
            if (res.status == 404) send_error(res, 404, 40401, "not found");
            else if (res.status == 405) send_error(res, 405, 40501, "method not allowed");
            else send_error(res, res.status, res.status * 100, "request failed");
            return;
        }
        res.status = res.status == 200 ? 404 : res.status;
        res.set_content("Not Found", "text/plain; charset=utf-8");
    });

    // ---------- ⑤ 静态页面 ----------
    if (!svr.set_mount_point("/", impl_->opts.web_dir)) {
        log_error(
                 "web dir not found: " + impl_->opts.web_dir +
                     " (frontend will be unavailable)");
    } else {
        log_info(
                 "static frontend mounted at / from " + impl_->opts.web_dir);
    }

    log_info(
             "server configured: port=" + std::to_string(impl_->opts.port) +
                 " data_dir=" + impl_->opts.data_dir);
    return true;
}

bool HttpServer::start() {
    if (impl_->running.load()) return false;  // 重复启动拒绝
    impl_->running.store(true);
    impl_->worker = std::thread([this]() {
        if (!impl_->svr.listen("0.0.0.0", impl_->opts.port)) {
            impl_->listen_failed.store(true);
            log_error(
                     "failed to bind port " + std::to_string(impl_->opts.port));
            impl_->shutdown_ctl.request_stop();  // 唤醒主线程进入收尾/退出
        }
    });
    return true;
}

void HttpServer::stop() noexcept {
    if (impl_ == nullptr) return;
    // svr.stop() 幂等：未监听时调用无副作用（与原 main 行为一致）。
    impl_->svr.stop();
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->running.store(false);
}

bool HttpServer::listen_failed() const {
    return impl_->listen_failed.load();
}

}  // namespace collector
