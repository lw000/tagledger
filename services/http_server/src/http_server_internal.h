#pragma once

// HttpServer 私有实现头（S3）：仅限 services/http_server/src/ 内部 include，
// 外部文件 include 本头即违规。

#include "http_server/http_server.h"
#include "http_server/whitelist.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <string>
#include <thread>

namespace collector {

struct HttpServer::Impl {
    const AppOptions opts;        // 值拷贝（AppOptions 小对象，避免引用生命周期问题）
    PointStore& store;            // 组合注入（B5）
    ShutdownController& shutdown_ctl;
    Whitelist whitelist;          // 内部白名单：由 HttpServer 统一管理
    httplib::Server svr;
    std::thread worker;           // 监听线程
    std::atomic<bool> running{false};
    std::atomic<bool> listen_failed{false};

    Impl(const AppOptions& o, PointStore& s, ShutdownController& sc)
        : opts(o), store(s), shutdown_ctl(sc) {}
};

// 共享响应 helper（定义于 routes.cpp，http_server.cpp 的中间件/兜底亦使用）
void send_json(httplib::Response& res, int status, const nlohmann::json& body);
void send_ok(httplib::Response& res, const nlohmann::json& data);
void send_error(httplib::Response& res, int http_status, int biz_code,
                const std::string& message,
                nlohmann::json details = nullptr);

// 业务路由注册（定义于 routes.cpp，init() 装配阶段调用）
void setup_routes(HttpServer::Impl& impl);

}  // namespace collector
