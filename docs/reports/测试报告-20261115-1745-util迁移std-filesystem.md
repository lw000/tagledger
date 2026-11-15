# 测试报告 — util 文件操作层迁移 std::filesystem（方案 A）+ GCC 16 适配

- 时间：2026-11-15 17:45
- 交付性质：代码变更（按 R 系列评审、人工确认的方案 A）

## 1. 测试范围

**改动模块**（对应 CHANGELOG 涉及文件）：

- `common/util.cpp`：文件操作层内部实现迁移为 `std::filesystem`（`error_code` 重载）
- `common/util.h`：层说明注释更新（公开 API 未变）
- `apps/tagledger/main.cpp`：新增前置 `#include <cstdint>`（GCC 16 + cxxopts 2.2.0 适配）
- `docs/03`、`docs/06`、`docs/CHANGELOG.md`：文档同步

**不在本次范围**：`csv_store`/`http_server`/`app_config` 等调用方代码（公开 API 不变，零改动）；性能基准基线对比（仅记录本次数值）；Linux/macOS 分支编译验证（Windows 单机环境）；`.vscode/launch.json` gdb 路径（未获确认，未动）。

## 2. 测试方法

环境：Windows，MinGW GCC 16.2.0（C:\mingw64）+ CMake 4.5.0；MSVC VS 2019 生成器。命令可复制重放（项目根目录执行）：

```bat
:: 1) MinGW 全新构建（旧 build/ 为 GCC 8.1 缓存，已删除重建）
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8

:: 2) MSVC 全新构建（旧 build-vs/ 为更名前路径缓存，已删除重建）
vs-debug.bat

:: 3) 测试套件（单元+退出+冒烟+并发+优雅退出；性能基准用 --skip-perf 跳过）
python tests\scripts\run_tests.py --skip-perf

:: 4) 单元/退出计数
build\tagledger_tests.exe [unit]
build\tagledger_tests.exe [shutdown]

:: 5) MSVC 版全量（含 [perf]）
build-vs\Debug\tagledger_tests.exe
```

## 3. 测试结果

| # | 检查项 | 结果 | 证据（退出码/输出） |
| --- | --- | --- | --- |
| 1 | MinGW（GCC 16.2.0）全新构建 | **通过** | exit 0；`Built target tagledger/tagledger_tests/tagledger_common/csv_store/http_server`；零 error 零 warning（-Wall -Wextra） |
| 2 | MSVC（VS 2019）全新构建 | **通过** | vs-debug.bat exit 0，`Build OK (Debug)` |
| 3 | `run_tests.py --skip-perf` 全套件 | **通过** | exit 0，`ALL TEST STEPS PASSED`（冒烟 22 项 PASS；并发 20/20 无丢失；优雅退出 8 项 PASS：管理端点 200→10s 内 exit 0→无 .tmp 残留→重启数据完整） |
| 4 | 单元测试（MinGW） | **通过** | `[unit]`：All tests passed（116 assertions in 23 test cases）；`[shutdown]`：7 assertions in 5 test cases（与迁移前基线一致） |
| 5 | MSVC 版全量测试（含 perf） | **通过** | All tests passed（118 assertions in 26 test cases）exit 0；perf 均值：load+parse 54.1ms、白名单命中 40.5us/未命中 66.0us、单点追加 78.8ms（仅记录，未做基线对比） |
| 6 | 调用方零改动核对 | **通过** | 调用方（csv_store/app_config/tests）grep 核对未修改，公开 API 签名未变 |

**行为语义核对**：`file_exists`（存在即真，查询失败视为否）、`make_dirs`（空串拒绝、已存在目录成功）、`rename_replace`（替换式+失败回退）、`list_csv_files`（仅普通文件、`.csv` 大小写不敏感、排序输出）均与迁移前等价；唯一刻意收紧：对"同名普通文件"调用 `make_dirs` 现返回 false（旧实现误返回 true），属修正而非回归，无调用方依赖该旧行为。

## 4. 已知问题

1. **`.vscode/launch.json` gdb 路径失效**：仍指向已卸载的 GCC 8.1.0 路径，MinGW 调试配置暂不可用（待人工确认后更新为 `C:\mingw64\bin\gdb.exe`）；MSVC cppvsdbg 配置不受影响。
2. **cxxopts/cstdint 适配为 F2 顺序例外**：`main.cpp` 中 `<cstdint>` 先于 third_party 包含（全仓唯一已登记例外，见 docs/06 附录 A"C++17 工具链适配登记"）；若未来升级 cxxopts 版本（须走 F5/Q6）可移除。
3. **性能数值未与基线对比**：本次仅记录 MSVC 下 perf 均值，与旧工具链基线的对比留待后续 perf 专项；`run_tests.py --skip-perf` 场景下未重跑 MinGW 版 [perf]。
4. **signal_entry 用例在 Windows 上按设计 SKIP**（控制台信号不可靠注入），既有行为，非本次引入。
