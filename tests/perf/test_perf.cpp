// 性能基准。命名规则："perf/<对象>/<操作>"，分组标签 [perf][<module>]。
// 运行：collector_tests "[perf]"（普通单元测试不运行本组）。
#include "csv_store/csv_store.h"
#include "common/util.h"
#include "http_server/whitelist.h"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using collector::Point;
using collector::PointStore;
using collector::Whitelist;
using collector::WhitelistRule;

namespace {
constexpr int kRows = 1000;
using collector::list_csv_files;
using collector::path_join;
using collector::remove_file;

// 准备含 kRows 行数据的临时文件，返回使用的文件名（不含扩展名）。
std::string prepare_store(PointStore& store, const std::string& name) {
    std::string file;
    if (store.create_file(name, &file).code != collector::StoreError::kOk) return "";
    std::vector<std::pair<std::optional<int64_t>, Point>> items;
    items.reserve(kRows);
    for (int i = 0; i < kRows; ++i) {
        items.emplace_back(std::nullopt,
                           Point{"perf_pt_" + std::to_string(i),
                                 std::to_string(40000 + i), "io",
                                 i % 2 ? "int" : "float", "rw", "bench"});
    }
    std::vector<PointStore::BatchItemResult> results;
    store.write_points_batch(name, items, &results);
    return name;
}

struct PerfFixture {
    std::string dir = path_join("build_test_data", "perf");
    PointStore store{dir};
    PerfFixture() { file_ = prepare_store(store, "perf_rows"); }
    ~PerfFixture() {
        for (const auto& f : list_csv_files(dir)) remove_file(path_join(dir, f));
    }
    std::string file_;
};
}  // namespace

TEST_CASE("perf/store/batch read full scan", "[perf][store]") {
    PerfFixture fx;
    int64_t total = 0;
    std::vector<collector::PointRow> rows;
    REQUIRE(fx.store.read_points(fx.file_, 0, 0, &total, &rows).code ==
            collector::StoreError::kOk);
    REQUIRE(total == kRows);

    BENCHMARK("load+parse 1000-row csv") {
        std::vector<collector::PointRow> out;
        int64_t n = 0;
        return fx.store.read_points(fx.file_, 0, 0, &n, &out);
    };
}

TEST_CASE("perf/store/single point write (load-modify-save)", "[perf][store]") {
    PerfFixture fx;
    BENCHMARK("append one point (full file rewrite)") {
        Point p{"bench_dyn", "49999", "io", "int", "rw", "bench"};
        // 同名会被重名拒绝导致每次落盘路径不同：改用固定 update 到第 0 行
        return fx.store.write_point(fx.file_, int64_t{0}, p, nullptr, nullptr);
    };
}

TEST_CASE("perf/whitelist/match against rule set", "[perf][whitelist]") {
    Whitelist w;
    for (int i = 0; i < 64; ++i) {
        WhitelistRule r;
        r.kind = WhitelistRule::Kind::kCidr;
        r.value = "10." + std::to_string(i) + ".0.0";
        r.prefix = 16;
        w.add_rule(r);
    }
    WhitelistRule exact;
    exact.kind = WhitelistRule::Kind::kIp;
    exact.value = "::1";
    w.add_rule(exact);

    BENCHMARK("64 cidr rules + 1 exact, hit last") {
        return w.allowed("::1", "");
    };
    BENCHMARK("64 cidr rules, miss") {
        return w.allowed("172.16.0.1", "");
    };
}
