#include "csv_store/csv_store.h"

#include <rapidcsv.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <utility>

#include "common/logger.h"
#include "common/util.h"

namespace collector {

namespace {

constexpr const char* kHeader = "name,io_address,address_type,point_type,rw,desc";
constexpr const char* kBom = "\xEF\xBB\xBF";

bool is_valid_enum(const std::string& v, std::initializer_list<const char*> set) {
    return std::any_of(set.begin(), set.end(), [&](const char* c) { return v == c; });
}

std::string csv_escape(const std::string& f) {
    if (f.find_first_of(",\"\r\n") == std::string::npos) return f;
    std::string out = "\"";
    for (char c : f) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::vector<std::string> point_fields(const Point& p) {
    return {p.name, p.io_address, p.address_type, p.point_type, p.rw, p.desc};
}

}  // namespace

PointStore::PointStore(std::string data_dir) : data_dir_(std::move(data_dir)) {
    if (!make_dirs(data_dir_))
        log_error("cannot create data dir: " + data_dir_);
}

std::string PointStore::normalize_csv_name(const std::string& raw) {
    if (!is_safe_name(raw)) return {};
    return raw + ".csv";
}

std::string PointStore::validate_point(const Point& p) {
    if (p.name.empty() || p.name.size() > 64) return "name must be 1..64 chars";
    if (p.io_address.empty() || p.io_address.size() > 64)
        return "io_address must be 1..64 chars";
    if (!is_valid_enum(p.address_type, {"memory", "io", "constant"}))
        return "address_type must be memory|io|constant";
    if (!is_valid_enum(p.point_type, {"bool", "int", "uint", "float", "double", "string"}))
        return "point_type must be bool|int|uint|float|double|string";
    if (!is_valid_enum(p.rw, {"rw", "ro", "wo"})) return "rw must be rw|ro|wo";
    if (p.desc.size() > 255) return "desc must be <=255 chars";
    return {};
}

std::shared_ptr<PointStore::FileLock> PointStore::get_lock(const std::string& csv_path,
                                                           bool create) const {
    std::lock_guard<std::mutex> g(locks_mu_);
    auto it = locks_.find(csv_path);
    if (it != locks_.end()) return it->second;
    if (!create && !file_exists(csv_path)) return nullptr;
    auto lk = std::make_shared<FileLock>();
    locks_.emplace(csv_path, lk);
    return lk;
}

StoreResult PointStore::load_rows_unlocked(const std::string& csv_path,
                                           std::vector<Point>* rows) const {
    if (!file_exists(csv_path)) return {StoreError::kFileNotFound, ""};
    try {
        rapidcsv::Document doc(csv_path, rapidcsv::LabelParams(0, -1),
                               rapidcsv::SeparatorParams(',', false, true));
        rows->clear();
        const int ncols = static_cast<int>(doc.GetColumnCount());
        const int nrows = static_cast<int>(doc.GetRowCount());
        rows->reserve(nrows);
        for (int r = 0; r < nrows; ++r) {
            Point p;
            p.name = ncols > 0 ? doc.GetCell<std::string>(0, r) : "";
            p.io_address = ncols > 1 ? doc.GetCell<std::string>(1, r) : "";
            p.address_type = ncols > 2 ? doc.GetCell<std::string>(2, r) : "";
            p.point_type = ncols > 3 ? doc.GetCell<std::string>(3, r) : "";
            p.rw = ncols > 4 ? doc.GetCell<std::string>(4, r) : "";
            p.desc = ncols > 5 ? doc.GetCell<std::string>(5, r) : "";
            if (p.name.empty() && p.io_address.empty()) continue;  // 跳过空行
            rows->push_back(std::move(p));
        }
        return {StoreError::kOk, ""};
    } catch (const std::exception& e) {
        return {StoreError::kIoError, std::string("parse csv failed: ") + e.what()};
    }
}

StoreResult PointStore::save_rows_unlocked(const std::string& csv_path,
                                           const std::vector<Point>& rows) const {
    const std::string tmp = csv_path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return {StoreError::kIoError, "cannot open temp file: " + tmp};
        f << kBom << kHeader << "\n";
        for (const auto& p : rows) {
            const auto fs_ = point_fields(p);
            for (size_t i = 0; i < fs_.size(); ++i) {
                if (i) f << ",";
                f << csv_escape(fs_[i]);
            }
            f << "\n";
        }
        f.flush();
        if (!f) {
            f.close();
            std::remove(tmp.c_str());
            return {StoreError::kIoError, "write temp file failed: " + tmp};
        }
    }
    if (!rename_replace(tmp, csv_path)) {
        std::remove(tmp.c_str());
        return {StoreError::kIoError, "atomic rename failed: " + tmp + " -> " + csv_path};
    }
    return {StoreError::kOk, ""};
}

int64_t PointStore::find_duplicate_unlocked(const std::vector<Point>& rows,
                                            const std::string& name,
                                            int64_t skip_index) {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (static_cast<int64_t>(i) == skip_index) continue;
        if (rows[i].name == name) return static_cast<int64_t>(i);
    }
    return -1;
}

std::vector<PointStore::FileInfo> PointStore::list_files() const {
    std::vector<FileInfo> out;
    for (const auto& fname : list_csv_files(data_dir_)) {
        FileInfo fi;
        fi.name = fname;
        std::vector<Point> rows;
        auto lk = get_lock(path_join(data_dir_, fname), false);
        if (lk) {
            std::shared_lock<std::shared_mutex> g(lk->mutex);
            if (load_rows_unlocked(path_join(data_dir_, fname), &rows).code ==
                StoreError::kOk)
                fi.rows = static_cast<int64_t>(rows.size());
        }
        out.push_back(std::move(fi));
    }
    std::sort(out.begin(), out.end(),
              [](const FileInfo& a, const FileInfo& b) { return a.name < b.name; });
    return out;
}

StoreResult PointStore::create_file(const std::string& raw_name, std::string* out_name) {
    auto name = normalize_csv_name(raw_name);
    if (name.empty()) return {StoreError::kInvalidParam, "invalid file name"};
    std::string full = path_join(data_dir_, name);
    if (file_exists(full)) return {StoreError::kDuplicateName, "file already exists"};
    auto lk = get_lock(full, true);
    std::unique_lock<std::shared_mutex> g(lk->mutex);
    if (file_exists(full)) return {StoreError::kDuplicateName, "file already exists"};
    std::vector<Point> empty;
    auto r = save_rows_unlocked(full, empty);
    if (r.code != StoreError::kOk) return r;
    if (out_name) *out_name = name;
    log_info("created csv file: " + name);
    return {StoreError::kOk, ""};
}

StoreResult PointStore::read_point(const std::string& raw_file, int64_t index,
                                   PointRow* out) const {
    auto name = normalize_csv_name(raw_file);
    if (name.empty()) return {StoreError::kInvalidParam, "invalid file name"};
    if (index < 0) return {StoreError::kRowNotFound, ""};
    std::string full = path_join(data_dir_, name);
    auto lk = get_lock(full, false);
    if (!lk) return {StoreError::kFileNotFound, ""};
    std::shared_lock<std::shared_mutex> g(lk->mutex);
    std::vector<Point> rows;
    auto r = load_rows_unlocked(full, &rows);
    if (r.code != StoreError::kOk) return r;
    if (index >= static_cast<int64_t>(rows.size())) return {StoreError::kRowNotFound, ""};
    if (out) *out = PointRow{index, rows[static_cast<size_t>(index)]};
    return {StoreError::kOk, ""};
}

StoreResult PointStore::read_points(const std::string& raw_file, int64_t start,
                                    int64_t count, int64_t* total,
                                    std::vector<PointRow>* out) const {
    auto name = normalize_csv_name(raw_file);
    if (name.empty()) return {StoreError::kInvalidParam, "invalid file name"};
    if (start < 0 || count < 0 || count > 10000)
        return {StoreError::kInvalidParam, "start/count out of range"};
    std::string full = path_join(data_dir_, name);
    auto lk = get_lock(full, false);
    if (!lk) return {StoreError::kFileNotFound, ""};
    std::shared_lock<std::shared_mutex> g(lk->mutex);
    std::vector<Point> rows;
    auto r = load_rows_unlocked(full, &rows);
    if (r.code != StoreError::kOk) return r;
    const int64_t n = static_cast<int64_t>(rows.size());
    if (total) *total = n;
    if (start > n) start = n;
    int64_t end = (count == 0) ? n : std::min(n, start + count);
    if (out) {
        for (int64_t i = start; i < end; ++i)
            out->push_back(PointRow{i, rows[static_cast<size_t>(i)]});
    }
    return {StoreError::kOk, ""};
}

StoreResult PointStore::write_point(const std::string& raw_file,
                                    const std::optional<int64_t>& index,
                                    const Point& point, int64_t* out_index,
                                    bool* out_appended) {
    auto name = normalize_csv_name(raw_file);
    if (name.empty()) return {StoreError::kInvalidParam, "invalid file name"};
    if (auto err = validate_point(point); !err.empty())
        return {StoreError::kInvalidParam, err};
    if (index && *index < 0) return {StoreError::kRowNotFound, ""};

    std::string full = path_join(data_dir_, name);
    auto lk = get_lock(full, true);
    std::unique_lock<std::shared_mutex> g(lk->mutex);

    std::vector<Point> rows;
    auto r = load_rows_unlocked(full, &rows);
    if (r.code != StoreError::kOk) return r;

    bool append = !index.has_value();
    if (!append && *index >= static_cast<int64_t>(rows.size()))
        return {StoreError::kRowNotFound, ""};
    int64_t target = append ? static_cast<int64_t>(rows.size()) : *index;

    if (auto dup = find_duplicate_unlocked(rows, point.name, append ? -1 : target);
        dup >= 0)
        return {StoreError::kDuplicateName,
                "duplicate name at row " + std::to_string(dup)};

    if (append) rows.push_back(point);
    else rows[static_cast<size_t>(target)] = point;

    r = save_rows_unlocked(full, rows);
    if (r.code != StoreError::kOk) return r;
    if (out_index) *out_index = target;
    if (out_appended) *out_appended = append;
    return {StoreError::kOk, ""};
}

StoreResult PointStore::write_points_batch(
    const std::string& raw_file,
    const std::vector<std::pair<std::optional<int64_t>, Point>>& items,
    std::vector<BatchItemResult>* results) {
    auto name = normalize_csv_name(raw_file);
    if (name.empty()) return {StoreError::kInvalidParam, "invalid file name"};
    if (items.empty()) return {StoreError::kInvalidParam, "items must not be empty"};
    if (items.size() > 10000)
        return {StoreError::kInvalidParam, "items exceeds limit 10000"};

    std::string full = path_join(data_dir_, name);
    auto lk = get_lock(full, true);
    std::unique_lock<std::shared_mutex> g(lk->mutex);

    std::vector<Point> rows;
    auto r = load_rows_unlocked(full, &rows);
    if (r.code != StoreError::kOk) return r;

    // 全量校验（任一失败则整批拒绝，不落盘）
    std::vector<BatchItemResult> staged;
    staged.reserve(items.size());
    for (const auto& [idx, point] : items) {
        if (auto err = validate_point(point); !err.empty())
            return {StoreError::kInvalidParam, "item " + std::to_string(staged.size()) +
                                                   ": " + err};
        bool append = !idx.has_value();
        if (!append && *idx >= static_cast<int64_t>(rows.size()))
            return {StoreError::kRowNotFound,
                    "item " + std::to_string(staged.size()) + ": row not found"};
        int64_t target = append ? static_cast<int64_t>(rows.size()) : *idx;
        if (auto dup = find_duplicate_unlocked(rows, point.name, append ? -1 : target);
            dup >= 0)
            return {StoreError::kDuplicateName,
                    "item " + std::to_string(staged.size()) +
                        ": duplicate name at row " + std::to_string(dup)};
        if (append) rows.push_back(point);
        else rows[static_cast<size_t>(target)] = point;
        staged.push_back(BatchItemResult{target, append});
    }

    r = save_rows_unlocked(full, rows);
    if (r.code != StoreError::kOk) return r;
    if (results) *results = std::move(staged);
    return {StoreError::kOk, ""};
}

}  // namespace collector
