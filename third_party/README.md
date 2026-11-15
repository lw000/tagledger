# third_party — 第三方依赖目录

本目录是 tagledger 项目的**唯一第三方依赖存放点**（依赖封闭，`docs/06-技术架构文档.md` 附录 A 约束 2）。所有库以源码 vendored 形式随仓库分发，不使用包管理器，不引入目录外依赖。

## 严禁修改（F5）

> **本目录下所有文件严禁修改，唯一例外：各库的 `README.md`**（允许更新以登记库说明）。
> 判定标准：除 README.md 外出现任何文件改动（含源码、头文件、构建脚本、LICENSE）即违规。

- 升级库 = 整体替换该库目录（经 F5/Q6 流程登记后执行），不做手工 patch。
- 项目侧适配（如 `main.cpp` 前置 `#include <cstdint>`）一律写在项目代码中，不改库源码。

## 依赖白名单（七库，与 docs/06 第 5 节选型表同步维护）

| 库 | 目录 | 版本 | 形态 | 项目内用途 | 使用位置 |
| --- | --- | --- | --- | --- | --- |
| cpp-httplib | `cpp-httplib/` | 0.66.0 | 单头 | HTTP 服务（线程池、`set_pre_routing_handler` 白名单中间件挂接点） | `services/http_server/` |
| nlohmann/json | `nlohmann/` | 3.12.0 | 单头 | JSON 序列化/反序列化（统一包络） | `services/http_server/` |
| rapidcsv | `rapidcsv/` | 9.09 | 单头 | CSV 解析（RFC 4180 引号字段、自动剥 BOM） | `services/csv_store/` |
| Catch2 | `catch2/` | 3.16 | 编译库 | 单元测试 + 性能基准（BENCHMARK）、CTest 集成 | `tests/` |
| cxxopts | `cxxopts/` | 2.2.0 | 单头 | 命令行参数解析（强制，硬性约束 13；`main.cpp` 须前置 `#include <cstdint>`） | `apps/tagledger/main.cpp` |
| toml++ | `toml/` | 3.4.0 | 单头 | 配置文件解析（强制，硬性约束 13；`config/app.toml`） | `common/app_config.cpp` |
| spdlog | `spdlog/` | 1.17.0 | 编译库（`SPDLOG_COMPILED_LIB`，MSVC 须 `/utf-8`） | 日志（经 `common/logger` 封装统一入口；rotating 10MB×100、异步队列、bundled fmt） | `common/logger` |

新增库必须先走 F5/Q6 流程：更新 `docs/06-技术架构文档.md` 附录 A 白名单并写明选型依据，经人工确认后方可整体引入，并同步登记本表。

## 使用约定

- **包含根**：各库头文件经构建系统提供的 include 根引用（如 `<catch2/...>`、`third_party` 与库目录由 CMake 注入），源码中禁止相对路径跳出（F3）。
- **构建期生成物**：`catch_user_config.hpp` 为构建期生成，勿手工创建（附录 A 约束 9）。
- **工具链登记的固有告警**：toml++ 在 `-Wall` 下有 2 处固有告警（附录 A 约束 13，已知问题，不修改库源码消除）。

## 目录清单

```
third_party/
├─ catch2/          Catch2 v3 单元测试框架（编译库）
├─ cpp-httplib/     HTTP server/client 单头库
├─ cxxopts/         命令行参数解析单头库
├─ nlohmann/        JSON 单头库（json.hpp / json_fwd.hpp）
├─ rapidcsv/        CSV 解析单头库
├─ spdlog/          日志库（编译库 + bundled fmt）
├─ toml/            toml++ 单头库
└─ README.md        本文件（各库子目录内的 README.md 为上游原文/登记说明）
```

权威出处：依赖白名单与选型理由见 `docs/06-技术架构文档.md` 第 5 节与附录 A；本文件为 third_party 目录的使用说明与禁改规则索引。
