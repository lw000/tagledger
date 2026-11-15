// 点位存储域单元测试。命名规则："<模块>/<被测行为>"，分组标签 [unit][store]。
#include "csv_store/csv_store.h"
#include "common/util.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fstream>

using collector::Point;
using collector::PointRow;
using collector::PointStore;
using collector::StoreError;

namespace {
// 每个用例独立临时目录：跨平台路径处理统一走 util 的 path_join（'/' 分隔符）。
using collector::list_csv_files;
using collector::path_join;
using collector::remove_file;

struct StoreFixture {
    std::string dir;
    explicit StoreFixture(std::string name) : dir(path_join("build_test_data", name)) {
        for (const auto& f : list_csv_files(dir)) remove_file(path_join(dir, f));
    }
    ~StoreFixture() {
        for (const auto& f : list_csv_files(dir)) remove_file(path_join(dir, f));
    }
    PointStore store() const { return PointStore(dir); }
};
}  // namespace

TEST_CASE("store/create file lifecycle", "[unit][store]") {
    StoreFixture fx("create");
    auto store = fx.store();
    std::string name;

    REQUIRE(store.create_file("unit_test", &name).code == StoreError::kOk);
    REQUIRE(name == "unit_test.csv");

    // 重名拒绝
    REQUIRE(store.create_file("unit_test", &name).code == StoreError::kDuplicateName);
    // 目录穿越 / 非法字符拒绝
    REQUIRE(store.create_file("../evil", &name).code == StoreError::kInvalidParam);
    REQUIRE(store.create_file("a b", &name).code == StoreError::kInvalidParam);
    REQUIRE(store.create_file("", &name).code == StoreError::kInvalidParam);
}

TEST_CASE("store/point field validation", "[unit][store]") {
    Point ok{"temp1", "40001", "io", "float", "rw", "1#炉温"};
    REQUIRE(PointStore::validate_point(ok).empty());

    SECTION("name empty/too long") {
        Point p = ok; p.name = "";
        REQUIRE_FALSE(PointStore::validate_point(p).empty());
        p.name = std::string(65, 'x');
        REQUIRE_FALSE(PointStore::validate_point(p).empty());
    }
    SECTION("io_address empty") {
        Point p = ok; p.io_address = "";
        REQUIRE_FALSE(PointStore::validate_point(p).empty());
    }
    SECTION("address_type enum") {
        Point p = ok; p.address_type = "oops";
        REQUIRE_FALSE(PointStore::validate_point(p).empty());
    }
    SECTION("point_type enum") {
        Point p = ok; p.point_type = "int64";
        REQUIRE_FALSE(PointStore::validate_point(p).empty());
    }
    SECTION("rw enum") {
        Point p = ok; p.rw = "read";
        REQUIRE_FALSE(PointStore::validate_point(p).empty());
    }
    SECTION("desc too long") {
        Point p = ok; p.desc = std::string(256, 'd');
        REQUIRE_FALSE(PointStore::validate_point(p).empty());
    }
}

TEST_CASE("store/single write append and update", "[unit][store]") {
    StoreFixture fx("single_write");
    auto store = fx.store();
    std::string file; REQUIRE(store.create_file("f", &file).code == StoreError::kOk);

    int64_t idx = -1; bool appended = false;
    Point p1{"temp1", "40001", "io", "float", "rw", "1#炉温"};
    REQUIRE(store.write_point("f", std::nullopt, p1, &idx, &appended).code == StoreError::kOk);
    REQUIRE(idx == 0);
    REQUIRE(appended);

    // 带 index → 修改
    p1.desc = "已修改";
    REQUIRE(store.write_point("f", int64_t{0}, p1, &idx, &appended).code == StoreError::kOk);
    REQUIRE_FALSE(appended);
    PointRow row;
    REQUIRE(store.read_point("f", 0, &row).code == StoreError::kOk);
    REQUIRE(row.point.desc == "已修改");

    // 越界修改
    REQUIRE(store.write_point("f", int64_t{99}, p1, &idx, &appended).code ==
            StoreError::kRowNotFound);
    // 重名拒绝
    REQUIRE(store.write_point("f", std::nullopt,
                              Point{"temp1", "40002", "io", "int", "ro", ""},
                              &idx, &appended).code == StoreError::kDuplicateName);
}

TEST_CASE("store/batch write is atomic on failure", "[unit][store]") {
    StoreFixture fx("batch");
    auto store = fx.store();
    std::string file; REQUIRE(store.create_file("f", &file).code == StoreError::kOk);
    // 先落一行，批量写的第一个条目（index=0）才能走"修改"语义
    REQUIRE(store.write_point("f", std::nullopt,
                              Point{"temp1", "40001", "io", "float", "rw", "初版"},
                              nullptr, nullptr).code == StoreError::kOk);

    std::vector<std::pair<std::optional<int64_t>, Point>> items = {
        {0, Point{"temp1", "40001", "io", "float", "rw", "初版"}},
        {std::nullopt, Point{"press1", "40002", "io", "int", "rw", ""}},
        {std::nullopt, Point{"const1", "100", "memory", "bool", "ro", ""}},
    };
    std::vector<PointStore::BatchItemResult> results;
    auto r = store.write_points_batch("f", items, &results);
    REQUIRE(r.code == StoreError::kOk);
    REQUIRE(results.size() == 3);
    REQUIRE((results[0].index == 0 && !results[0].appended));
    REQUIRE((results[1].index == 1 && results[1].appended));
    REQUIRE((results[2].index == 2 && results[2].appended));

    // 第二条重名 → 整批拒绝，文件保持 3 行
    std::vector<std::pair<std::optional<int64_t>, Point>> bad = {
        {std::nullopt, Point{"press2", "40003", "io", "int", "rw", ""}},
        {std::nullopt, Point{"press1", "40004", "io", "int", "rw", ""}},
    };
    REQUIRE(store.write_points_batch("f", bad, &results).code ==
            StoreError::kDuplicateName);
    int64_t total = 0;
    std::vector<PointRow> rows;
    REQUIRE(store.read_points("f", 0, 0, &total, &rows).code == StoreError::kOk);
    REQUIRE(total == 3);
    REQUIRE(rows[0].point.name == "temp1");
    REQUIRE(rows.size() == 3);
}

TEST_CASE("store/read pagination and not-found", "[unit][store]") {
    StoreFixture fx("read");
    auto store = fx.store();
    std::string file; REQUIRE(store.create_file("f", &file).code == StoreError::kOk);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(store.write_point("f", std::nullopt,
                                  Point{"p" + std::to_string(i), "a" + std::to_string(i),
                                        "io", "int", "rw", ""},
                                  nullptr, nullptr).code == StoreError::kOk);
    }

    int64_t total = 0;
    std::vector<PointRow> rows;
    REQUIRE(store.read_points("f", 1, 2, &total, &rows).code == StoreError::kOk);
    REQUIRE(total == 5);
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].index == 1);
    REQUIRE(rows[0].point.name == "p1");

    // count=0 → 到文件尾
    rows.clear();
    REQUIRE(store.read_points("f", 3, 0, &total, &rows).code == StoreError::kOk);
    REQUIRE(rows.size() == 2);

    // 越界 / 文件不存在 / 非法文件名
    PointRow row;
    REQUIRE(store.read_point("f", 99, &row).code == StoreError::kRowNotFound);
    REQUIRE(store.read_point("no_such", 0, &row).code == StoreError::kFileNotFound);
    REQUIRE(store.read_point("../evil", 0, &row).code == StoreError::kInvalidParam);
}

TEST_CASE("store/csv escaping roundtrip and utf8 bom", "[unit][store]") {
    StoreFixture fx("escape");
    auto store = fx.store();
    std::string file; REQUIRE(store.create_file("f", &file).code == StoreError::kOk);

    Point weird{"w,1", "addr\"x\"", "io", "string", "rw", "含,逗号\"引号\"换行不算"};
    int64_t idx = -1;
    REQUIRE(store.write_point("f", std::nullopt, weird, &idx, nullptr).code ==
            StoreError::kOk);
    REQUIRE(idx == 0);

    PointRow row;
    REQUIRE(store.read_point("f", 0, &row).code == StoreError::kOk);
    REQUIRE(row.point.name == "w,1");
    REQUIRE(row.point.io_address == "addr\"x\"");
    REQUIRE(row.point.desc == "含,逗号\"引号\"换行不算");

    // UTF-8 BOM 存在（Excel 兼容）
    std::ifstream f(path_join(fx.dir, "f.csv"), std::ios::binary);
    char bom[3] = {0};
    f.read(bom, 3);
    REQUIRE(bom[0] == '\xEF');
    REQUIRE(bom[1] == '\xBB');
    REQUIRE(bom[2] == '\xBF');
}

TEST_CASE("store/atomic temp file leaves no residue", "[unit][store]") {
    StoreFixture fx("atomic");
    auto store = fx.store();
    std::string file; REQUIRE(store.create_file("f", &file).code == StoreError::kOk);
    REQUIRE(store.write_point("f", std::nullopt,
                              Point{"p1", "1", "io", "int", "rw", ""},
                              nullptr, nullptr).code == StoreError::kOk);
    // 原子替换后不允许残留 .tmp 文件
    REQUIRE(list_csv_files(fx.dir).size() == 1);
    REQUIRE(list_csv_files(fx.dir)[0] == "f.csv");
}
