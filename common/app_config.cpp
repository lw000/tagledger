#include "common/app_config.h"

#include <toml.hpp>

#include "common/logger.h"
#include "common/util.h"

namespace collector {

bool load_app_config(const std::string& toml_path, AppOptions* out) {
    if (out == nullptr || !file_exists(toml_path)) return false;

    toml::table cfg;
    try {
        cfg = toml::parse_file(toml_path);
    } catch (const std::exception& e) {
        log_warning("invalid app config ignored: " + toml_path +
                    " (" + e.what() + ")");
        return false;
    }

    if (!out->cli_port && cfg.contains("port")) {
        if (auto v = cfg["port"].value<int>())
            out->port = *v;
        else
            log_warning("app config 'port' must be an integer; ignored");
    }
    if (!out->cli_data_dir && cfg.contains("data_dir")) {
        if (auto v = cfg["data_dir"].value<std::string>())
            out->data_dir = *v;
        else
            log_warning("app config 'data_dir' must be a string; ignored");
    }
    if (!out->cli_web_dir && cfg.contains("web_dir")) {
        if (auto v = cfg["web_dir"].value<std::string>())
            out->web_dir = *v;
        else
            log_warning("app config 'web_dir' must be a string; ignored");
    }

    // ---- [log] 段：日志等级/异步队列/保留策略（键缺省用 LogConfig 内置默认） ----
    if (cfg.contains("log")) {
        const toml::table* log_tbl = cfg["log"].as_table();
        if (log_tbl != nullptr) {
            const auto warn_bad = [](const char* key, const char* type) {
                log_warning(std::string("app config [log] '") + key +
                            "' must be " + type + "; ignored");
            };
            if (log_tbl->contains("level")) {
                if (auto v = (*log_tbl)["level"].value<std::string>())
                    out->log.level = *v;
                else
                    warn_bad("level", "a string");
            }
            if (log_tbl->contains("async")) {
                if (auto v = (*log_tbl)["async"].value<bool>())
                    out->log.async = *v;
                else
                    warn_bad("async", "a boolean");
            }
            if (log_tbl->contains("queue_size")) {
                if (auto v = (*log_tbl)["queue_size"].value<int>())
                    out->log.queue_size = *v;
                else
                    warn_bad("queue_size", "an integer");
            }
            if (log_tbl->contains("file_path")) {
                if (auto v = (*log_tbl)["file_path"].value<std::string>())
                    out->log.file_path = *v;
                else
                    warn_bad("file_path", "a string");
            }
            if (log_tbl->contains("max_file_mb")) {
                if (auto v = (*log_tbl)["max_file_mb"].value<int>())
                    out->log.max_file_mb = *v;
                else
                    warn_bad("max_file_mb", "an integer");
            }
            if (log_tbl->contains("max_files")) {
                if (auto v = (*log_tbl)["max_files"].value<int>())
                    out->log.max_files = *v;
                else
                    warn_bad("max_files", "an integer");
            }
        } else {
            log_warning("app config '[log]' must be a table; ignored");
        }
    }

    log_info("app config loaded: " + toml_path);
    return true;
}

}  // namespace collector
