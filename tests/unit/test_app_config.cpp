#include "common/app_config.h"
#include "common/util.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using collector::AppOptions;
using collector::load_app_config;
using collector::make_dirs;
using collector::path_join;
using collector::remove_file;

namespace {
// 每个用例独立临时目录（与 test_csv_store 同模式；清理已知文件）。
struct CfgFixture {
    std::string dir;
    std::string toml_path;

    explicit CfgFixture(std::string name)
        : dir(path_join("build_test_data", name)),
          toml_path(path_join(dir, "app.toml")) {
        make_dirs(dir);
        remove_file(toml_path);
    }
    ~CfgFixture() { remove_file(toml_path); }

    void write(const std::string& content) {
        std::ofstream out(toml_path, std::ios::binary);
        out << content;
    }
};
}  // namespace

TEST_CASE("app_config/missing file keeps options and returns false",
          "[unit][app_config]") {
    CfgFixture fx("cfg_missing");
    AppOptions opts;
    opts.port = 9090;  // 非默认值，验证不被改动

    REQUIRE_FALSE(load_app_config(fx.toml_path, &opts));
    REQUIRE((opts.port == 9090 && opts.data_dir == "data"));
}

TEST_CASE("app_config/valid toml overrides non-cli keys", "[unit][app_config]") {
    CfgFixture fx("cfg_valid");
    fx.write("port = 9100\ndata_dir = \"cfg_data\"\nweb_dir = \"cfg_web\"\n");
    AppOptions opts;

    REQUIRE(load_app_config(fx.toml_path, &opts));
    REQUIRE((opts.port == 9100 && opts.data_dir == "cfg_data" &&
             opts.web_dir == "cfg_web" && opts.config_dir == "config"));
}

TEST_CASE("app_config/cli-set keys win over toml", "[unit][app_config]") {
    CfgFixture fx("cfg_cli");
    fx.write("port = 9200\ndata_dir = \"toml_dir\"\n");
    AppOptions opts;
    opts.port = 7000;  // 命令行已设置
    opts.cli_port = true;

    REQUIRE(load_app_config(fx.toml_path, &opts));
    REQUIRE((opts.port == 7000 && opts.data_dir == "toml_dir"));
}

TEST_CASE("app_config/malformed toml is tolerated with warn",
          "[unit][app_config]") {
    CfgFixture fx("cfg_bad");
    fx.write("this is [[ not valid toml");
    AppOptions opts;
    opts.port = 9090;

    REQUIRE_FALSE(load_app_config(fx.toml_path, &opts));
    REQUIRE(opts.port == 9090);
}

TEST_CASE("app_config/wrong value type is ignored per key",
          "[unit][app_config]") {
    CfgFixture fx("cfg_type");
    fx.write("port = \"not-a-number\"\ndata_dir = 123\n");
    AppOptions opts;

    REQUIRE(load_app_config(fx.toml_path, &opts));
    REQUIRE((opts.port == 8080 && opts.data_dir == "data"));
}

TEST_CASE("app_config/log section overrides defaults", "[unit][app_config]") {
    CfgFixture fx("cfg_log");
    fx.write("[log]\nlevel = \"debug\"\nasync = false\nqueue_size = 123\n"
             "file_path = \"x/y.log\"\nmax_file_mb = 5\nmax_files = 7\n");
    AppOptions opts;

    REQUIRE(load_app_config(fx.toml_path, &opts));
    REQUIRE((opts.log.level == "debug" && opts.log.async == false));
    REQUIRE((opts.log.queue_size == 123 && opts.log.max_file_mb == 5 &&
             opts.log.max_files == 7 && opts.log.file_path == "x/y.log"));
}

TEST_CASE("app_config/log wrong types fall back to defaults",
          "[unit][app_config]") {
    CfgFixture fx("cfg_log_bad");
    fx.write("[log]\nlevel = 123\nasync = \"yes\"\nqueue_size = \"many\"\n");
    AppOptions opts;

    REQUIRE(load_app_config(fx.toml_path, &opts));
    REQUIRE((opts.log.level == "info" && opts.log.async == true &&
             opts.log.queue_size == 8192));
}
