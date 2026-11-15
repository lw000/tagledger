// routes.cpp —— HttpServer 的业务路由（请求处理器）注册。
// 由 init() 装配阶段调用（setup_routes），统一包络/业务码体系见 docs/02 第 3 节。

#include "http_server_internal.h"

#include <optional>
#include <utility>
#include <vector>

namespace collector {

using nlohmann::json;

// 共享响应 helper：定义于命名空间外（http_server_internal.h 声明，
// 供本文件与 http_server.cpp 的中间件/异常兜底共用）。
void send_json(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

void send_ok(httplib::Response& res, const json& data) {
    send_json(res, 200, json{{"code", 0}, {"message", "ok"}, {"data", data}});
}

void send_error(httplib::Response& res, int http_status, int biz_code,
                const std::string& message, json details) {
    json body = {{"code", biz_code}, {"message", message}};
    if (!details.is_null()) body["details"] = details;
    send_json(res, http_status, body);
}

namespace {

json point_to_json(const Point& p) {
    return {{"name", p.name},
            {"io_address", p.io_address},
            {"address_type", p.address_type},
            {"point_type", p.point_type},
            {"rw", p.rw},
            {"desc", p.desc}};
}

Point point_from_json(const json& j) {
    Point p;
    p.name = j.at("name").get<std::string>();
    p.io_address = j.at("io_address").get<std::string>();
    p.address_type = j.at("address_type").get<std::string>();
    p.point_type = j.at("point_type").get<std::string>();
    p.rw = j.at("rw").get<std::string>();
    p.desc = j.value("desc", "");
    return p;
}

void send_store_error(httplib::Response& res, const StoreResult& r) {
    switch (r.code) {
        case StoreError::kOk:
            send_json(res, 200, json{{"code", 0}, {"message", "ok"}});
            break;
        case StoreError::kFileNotFound:
            send_error(res, 404, 40401, r.message.empty()
                                            ? store_error_message(r.code)
                                            : r.message);
            break;
        case StoreError::kRowNotFound:
            send_error(res, 404, 40402, r.message.empty()
                                            ? store_error_message(r.code)
                                            : r.message);
            break;
        case StoreError::kInvalidParam:
            send_error(res, 400, 40002, r.message.empty()
                                            ? store_error_message(r.code)
                                            : r.message);
            break;
        case StoreError::kDuplicateName:
            send_error(res, 409, 40901, r.message.empty()
                                            ? store_error_message(r.code)
                                            : r.message);
            break;
        default:
            send_error(res, 500, 50001, r.message.empty()
                                            ? store_error_message(r.code)
                                            : r.message);
            break;
    }
}

bool parse_body(const httplib::Request& req, httplib::Response& res, json* out) {
    try {
        *out = json::parse(req.body);
        return true;
    } catch (const std::exception&) {
        send_error(res, 400, 40001, "invalid json body");
        return false;
    }
}

// 兼容 nlohmann 3.1（无 contains()）
bool json_has(const json& j, const char* key) {
    return j.find(key) != j.end();
}

std::optional<int64_t> opt_index_from_json(const json& j) {
    if (!json_has(j, "index") || j.at("index").is_null()) return std::nullopt;
    return j.at("index").get<int64_t>();
}

}  // namespace

void setup_routes(HttpServer::Impl& impl) {
    httplib::Server& svr = impl.svr;
    PointStore& store = impl.store;
    ShutdownController& shutdown_ctl = impl.shutdown_ctl;

    // ---------- 业务路由 ----------
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        send_ok(res, json{{"version", "1.0.0"}});
    });

    // 优雅退出端点（受白名单保护）：触发 request_stop，进行中的请求全部完成后进程退出。
    svr.Post("/api/admin/shutdown",
             [&shutdown_ctl](const httplib::Request&, httplib::Response& res) {
                 shutdown_ctl.request_stop();
                 send_ok(res, json{{"stopping", true}});
             });

    // 列出 CSV 文件
    svr.Get("/api/v1/files",
            [&store](const httplib::Request&, httplib::Response& res) {
                json files = json::array();
                for (const auto& f : store.list_files())
                    files.push_back({{"name", f.name}, {"rows", f.rows}});
                send_ok(res,
                        json{{"files", files}, {"data_dir", store.data_dir()}});
            });

    // 新建 CSV 文件
    svr.Post("/api/v1/files", [&store](const httplib::Request& req,
                                       httplib::Response& res) {
        json body;
        if (!parse_body(req, res, &body)) return;
        if (!json_has(body, "name") || !body.at("name").is_string()) {
            send_error(res, 400, 40002, "field 'name' (string) is required");
            return;
        }
        std::string name;
        auto r = store.create_file(body.at("name").get<std::string>(), &name);
        if (r.code != StoreError::kOk) {
            send_store_error(res, r);
            return;
        }
        send_ok(res, json{{"name", name}, {"created", true}});
    });

    // 单读 / 批量读（query 参数区分）
    svr.Get("/api/v1/points", [&store](const httplib::Request& req,
                                       httplib::Response& res) {
        const bool has_file = req.has_param("file");
        if (!has_file) {
            send_error(res, 400, 40002, "query param 'file' is required");
            return;
        }
        const std::string file = req.get_param_value("file");

        // 单读：index 给定且无 start/count
        if (req.has_param("index")) {
            int64_t index = 0;
            try {
                index = std::stoll(req.get_param_value("index"));
            } catch (const std::exception&) {
                send_error(res, 400, 40002, "'index' must be an integer");
                return;
            }
            PointRow row;
            auto r = store.read_point(file, index, &row);
            if (r.code != StoreError::kOk) {
                send_store_error(res, r);
                return;
            }
            send_ok(res, json{{"index", row.index},
                              {"point", point_to_json(row.point)}});
            return;
        }

        // 批量读：start / count
        int64_t start = 0, count = 0;
        try {
            if (req.has_param("start"))
                start = std::stoll(req.get_param_value("start"));
            if (req.has_param("count"))
                count = std::stoll(req.get_param_value("count"));
        } catch (const std::exception&) {
            send_error(res, 400, 40002, "'start'/'count' must be integers");
            return;
        }
        int64_t total = 0;
        std::vector<PointRow> rows;
        auto r = store.read_points(file, start, count, &total, &rows);
        if (r.code != StoreError::kOk) {
            send_store_error(res, r);
            return;
        }
        json points = json::array();
        for (const auto& row : rows)
            points.push_back({{"index", row.index},
                              {"point", point_to_json(row.point)}});
        send_ok(res, json{{"total", total},
                          {"start", start},
                          {"count", rows.size()},
                          {"points", points}});
    });

    // 单写
    svr.Post("/api/v1/points", [&store](const httplib::Request& req,
                                        httplib::Response& res) {
        json body;
        if (!parse_body(req, res, &body)) return;
        if (!json_has(body, "file") || !body.at("file").is_string() ||
            !json_has(body, "point") || !body.at("point").is_object()) {
            send_error(res, 400, 40002,
                       "fields 'file'(string) and 'point'(object) are required");
            return;
        }
        try {
            Point p = point_from_json(body.at("point"));
            auto idx = opt_index_from_json(body);
            int64_t out_index = -1;
            bool appended = false;
            auto r = store.write_point(body.at("file").get<std::string>(), idx, p,
                                       &out_index, &appended);
            if (r.code != StoreError::kOk) {
                send_store_error(res, r);
                return;
            }
            send_ok(res, json{{"file", body.at("file").get<std::string>() + ".csv"},
                              {"index", out_index},
                              {"op", appended ? "append" : "update"}});
        } catch (const std::exception& e) {
            send_error(res, 400, 40002, std::string("invalid point field: ") + e.what());
        }
    });

    // 批量写
    svr.Post("/api/v1/points/batch", [&store](const httplib::Request& req,
                                              httplib::Response& res) {
        json body;
        if (!parse_body(req, res, &body)) return;
        if (!json_has(body, "file") || !body.at("file").is_string() ||
            !json_has(body, "items") || !body.at("items").is_array()) {
            send_error(res, 400, 40002,
                       "fields 'file'(string) and 'items'(array) are required");
            return;
        }
        try {
            std::vector<std::pair<std::optional<int64_t>, Point>> items;
            for (const auto& it : body.at("items"))
                items.emplace_back(opt_index_from_json(it), point_from_json(it.at("point")));

            std::vector<PointStore::BatchItemResult> results;
            auto r = store.write_points_batch(body.at("file").get<std::string>(), items,
                                              &results);
            if (r.code != StoreError::kOk) {
                send_store_error(res, r);
                return;
            }
            json arr = json::array();
            for (const auto& it : results)
                arr.push_back({{"index", it.index},
                               {"op", it.appended ? "append" : "update"},
                               {"code", 0}});
            send_ok(res, json{{"applied", arr.size()}, {"results", arr}});
        } catch (const std::exception& e) {
            send_error(res, 400, 40002, std::string("invalid item field: ") + e.what());
        }
    });

    // 未知 /api 路径/方法 → 统一 JSON 错误。
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
}

}  // namespace collector
