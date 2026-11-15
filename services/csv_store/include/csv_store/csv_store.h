#pragma once
// 点位 CSV 存储域：文件管理 + 行读写，线程安全，原子落盘。
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace collector {

struct Point {
    std::string name;
    std::string io_address;
    std::string address_type;  // memory | io | constant
    std::string point_type;    // bool | int | uint | float | double | string
    std::string rw;            // rw | ro | wo
    std::string desc;
};

struct PointRow {
    int64_t index = -1;
    Point point;
};

// 存储层错误码（与 docs/02-方案设计.md 3.0 对齐）
enum class StoreError {
    kOk = 0,
    kFileNotFound = 40401,
    kRowNotFound = 40402,
    kInvalidParam = 40002,
    kDuplicateName = 40901,
    kIoError = 50001,
};

inline const char* store_error_message(StoreError e) {
    switch (e) {
        case StoreError::kOk: return "ok";
        case StoreError::kFileNotFound: return "csv file not found";
        case StoreError::kRowNotFound: return "point row not found";
        case StoreError::kInvalidParam: return "invalid parameter";
        case StoreError::kDuplicateName: return "duplicate point name";
        case StoreError::kIoError: return "io error on csv file";
    }
    return "unknown error";
}

struct StoreResult {
    StoreError code = StoreError::kOk;
    std::string message;  // 附加说明（为空时取 store_error_message）
};

class PointStore {
public:
    explicit PointStore(std::string data_dir);

    // 文件管理
    struct FileInfo {
        std::string name;   // 含 .csv
        int64_t rows = 0;   // 数据行数（不含表头）
    };
    std::vector<FileInfo> list_files() const;
    StoreResult create_file(const std::string& raw_name, std::string* out_name);
    std::string data_dir() const { return data_dir_; }

    // 行读写
    StoreResult read_point(const std::string& raw_file, int64_t index, PointRow* out) const;
    StoreResult read_points(const std::string& raw_file, int64_t start, int64_t count,
                            int64_t* total, std::vector<PointRow>* out) const;
    // index: 无值 → 追加；有值 → 修改该行
    StoreResult write_point(const std::string& raw_file,
                            const std::optional<int64_t>& index,
                            const Point& point, int64_t* out_index, bool* out_appended);
    // 批量写事务：全部校验通过才落盘；失败返回首条错误并回滚（不落盘）。
    struct BatchItemResult {
        int64_t index = -1;
        bool appended = false;
    };
    StoreResult write_points_batch(const std::string& raw_file,
                                   const std::vector<std::pair<std::optional<int64_t>, Point>>& items,
                                   std::vector<BatchItemResult>* results);

    // 文件名规范化与校验：合法返回 "<name>.csv"，否则返回空串。
    static std::string normalize_csv_name(const std::string& raw);
    // 点位字段校验；通过返回空串，否则返回原因。
    static std::string validate_point(const Point& p);

private:
    struct FileLock {
        std::shared_mutex mutex;
    };

    std::shared_ptr<FileLock> get_lock(const std::string& csv_path, bool create) const;
    StoreResult load_rows_unlocked(const std::string& csv_path,
                                   std::vector<Point>* rows) const;
    StoreResult save_rows_unlocked(const std::string& csv_path,
                                   const std::vector<Point>& rows) const;
    static int64_t find_duplicate_unlocked(const std::vector<Point>& rows,
                                           const std::string& name, int64_t skip_index);

    std::string data_dir_;
    mutable std::mutex locks_mu_;
    mutable std::map<std::string, std::shared_ptr<FileLock>> locks_;
};

}  // namespace collector
