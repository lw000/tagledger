# third_party — 第三方依赖目录

本目录是 tagledger 项目的**唯一第三方依赖存放点**（依赖封闭，`docs/06-技术架构文档.md` 附录 A 约束 2）。所有库以源码 vendored 形式随仓库分发，不使用包管理器，不引入目录外依赖。

**目录状态（2026-11-21 盘点 + 人工裁决后）**：目录全部条目已纳入 git 分发，分两类——① **白名单库 7 个**（参与构建，第 2 节）；② **备用库区 29 项**（已入库、仅供检索备料，未接入构建、不在白名单内，启用须先走 F5/Q6 登记并经人工确认，第 6 节）。

## 1. 严禁修改（F5）

> **本目录下所有文件严禁修改，唯一例外：各库的 `README.md`**（允许更新以登记库说明）。
> 判定标准：除 README.md 外出现任何文件改动（含源码、头文件、构建脚本、LICENSE）即违规。
> 注：例外仅限文件名为 `README.md` 的文件；上游 `readme.txt` 等其他说明文件不在例外之列。

- 升级库 = 整体替换该库目录（经 F5/Q6 流程登记后执行），不做手工 patch。
- 项目侧适配一律写在项目代码中，不改库源码（历史案例：cxxopts 2.2.0 时代 `main.cpp` 前置 `#include <cstdint>`，随 3.3.1 升级已移除）。

## 2. 依赖白名单（七库，参与构建，与 docs/06 第 5 节选型表同步维护）

版本均以库内版本宏/头注释实际核对为准（2026-11-21 盘点）。

| 库 | 目录 | 版本（实际核对） | 形态 | 项目内用途 | 使用位置 |
| --- | --- | --- | --- | --- | --- |
| cpp-httplib | `cpp-httplib/` | 0.66.0（`CPPHTTPLIB_VERSION`） | 单头 | HTTP 服务（线程池、`set_pre_routing_handler` 白名单中间件挂接点） | `services/http_server/` |
| nlohmann/json | `nlohmann/` | 3.12.0（`NLOHMANN_JSON_VERSION_*`） | 单头 | JSON 序列化/反序列化（统一包络） | `services/http_server/` |
| rapidcsv | `rapidcsv/` | 9.09（头注释） | 单头 | CSV 解析（RFC 4180 引号字段、自动剥 BOM） | `services/csv_store/` |
| Catch2 | `catch2/` | 3.16.1（版本宏；docs/06 登记为 3.16） | 编译库（v3 完整源码树） | 单元测试 + 性能基准（BENCHMARK）、CTest 集成 | `tests/` |
| cxxopts | `cxxopts/` | 3.3.1（`CXXOPTS__VERSION_*`） | 单头 | 命令行参数解析（强制，硬性约束 13；自身包含 `<cstdint>`，GCC 16 适配内建） | `apps/tagledger/main.cpp` |
| toml++ | `toml/` | 3.4.0（`TOML_LIB_MAJOR/MINOR/PATCH`） | 单头 | 配置文件解析（强制，硬性约束 13；`config/app.toml`） | `common/app_config.cpp` |
| spdlog | `spdlog/` | 1.17.0（`version.h`；bundled fmt 12.1.0） | 编译库（`SPDLOG_COMPILED_LIB`，MSVC 须 `/utf-8`） | 日志（经 `common/logger` 封装统一入口；rotating 10MB×100、异步队列、bundled fmt） | `common/logger` |

新增/启用库必须先走 F5/Q6 流程：更新 `docs/06-技术架构文档.md` 附录 A 白名单并写明选型依据，经人工确认后方可整体引入，并同步登记本表。

## 3. 引用关系与依赖方向（F4）

`apps → services → common → tests` 对白名单库的消费关系（2026-11-21 核对，引用位置含 CMake include 根注入）：

- `apps/tagledger/main.cpp` → cxxopts
- `services/http_server/` → cpp-httplib、nlohmann/json
- `services/csv_store/` → rapidcsv
- `common/app_config.cpp` → toml++（经 `third_party/toml` 包含根）
- `common/logger` → spdlog（编译库 `spdlog_lib`）
- `tests/unit|perf` → Catch2（编译库 `catch2_lib`）

除上述七库外，目录内任何其他条目均不得出现在源码或 CMake 引用中。

## 4. 使用约定

- **包含根**：各库头文件经构建系统提供的 include 根引用（如 `<catch2/...>`、`<toml.hpp>`），包含根由 CMake 注入，源码中禁止相对路径跳出（F3）。
- **构建期生成物**：`catch_user_config.hpp` 为构建期生成，勿手工创建（附录 A 约束 9）。
- **工具链登记的固有告警**：toml++ 在 `-Wall` 下有 2 处固有告警（附录 A 约束 13，已知问题，不修改库源码消除）。

## 5. 版本裁决记录（2026-11-21，人工确认）

当日盘点发现两处被跟踪文件未登记的工作区覆盖，经人工裁决处理完毕：

| 条目 | 盘点发现 | 裁决 | 处理结果 |
| --- | --- | --- | --- |
| `cxxopts/cxxopts.hpp` | 登记版本 2.2.0（HEAD），工作区被覆盖为 3.3.1 | **更新**：登记升级 3.3.1（docs/06 选型表 + 硬性约束 13 已同步） | 3.3.1 自身包含 `<cstdint>`（头文件第 30 行），`main.cpp` 的 `<cstdint>` 前置适配随升级移除；构建零错误、全部测试通过 |
| `nlohmann/json.hpp`、`json_fwd.hpp` | HEAD 与工作区同为 3.12.0，但版权横幅等约 888 行差异（疑为重新覆盖的发行副本） | **回退**：恢复登记副本 | `git checkout -- third_party/nlohmann`，工作区与 HEAD 一致（3.12.0） |

## 6. 备用库区（29 项，已入库、未启用）

2026-11-21 经人工裁决随仓库分发入库（"新增的提交 git"），此前为未跟踪条目。全部经全仓 grep（源码 + CMake + 脚本）确认**零引用**；**启用任一条目前必须先走 F5/Q6 流程登记白名单并经人工确认**，启用前禁止任何 include/CMake 引用。部分条目存在职责重叠或已失去存在理由的备注，供未来选型时参考。

| 条目 | 库名 | 版本（核对） | 形态 | 备注 |
| --- | --- | --- | --- | --- |
| `tomlplusplus/` | toml++ | 3.4.0 | 裸单头 | 与白名单库 `toml/` **同库同版重复**，启用场景应直接用 `toml/` |
| `concurrentqueue/` | moodycamel::ConcurrentQueue | 未标注 | 3 头 | 无引用 |
| `rigtorp/` | rigtorp MPMCQueue/SPSCQueue | 未标注 | 双头 | 无引用；与 concurrentqueue 功能同类（非同库） |
| `thread_pool/` | BS::thread_pool | 5.1.0 | 单头 | 无引用 |
| `thread_pool_dp/` | dp::thread_pool | 未标注 | 双头 | 无引用；与 thread_pool 功能同类（非同库） |
| `boolinq/` | boolinq（Linq for C++） | 未标注 | 单头 | 无引用 |
| `cereal/` | cereal 序列化库 | 1.3.2 | 完整源码树（含 rapidjson/rapidxml） | 无引用，体积大 |
| `date/` | Howard Hinnant date/time | 未标注 | 多头（9 个 .h） | 无引用 |
| `doctest/` | doctest | 2.5.0 | 单头 + parts 拆分树 | 无引用；与 Catch2 测试职责重叠 |
| `eventpp/` | eventpp 事件分发 | 未标注 | 多头 | 无引用 |
| `exprtk/` | ExprTk 表达式库 | 未标注 | 单头（约 1.6MB） | 无引用，体积大 |
| `ghc/` | ghc::filesystem | 1.5.15 | 多头 | 无引用；`std::filesystem` 禁令已解除，启用必要性低 |
| `glob/` | p-ranav/glob | 未标注 | 单头 | 无引用 |
| `hash-library/` | Stephan Brumme hash-library | 未标注 | 多头多源 | 无引用 |
| `inja/` | inja 模板库 | 3.5.0 | 单头 | 无引用 |
| `json_struct/` | json_struct | 未标注 | 双头 | 无引用；与 nlohmann/json 职责重叠 |
| `magic_enum/` | magic_enum | 0.9.7 | 多头（9 个 .hpp） | 无引用 |
| `open62541-c/` | open62541（OPC UA） | 1.4.14 | amalgamated 双文件（约 16MB） | 无引用，体积最大 |
| `parallel_hashmap/` | parallel_hashmap（phmap） | 2.0.0 | 多头 | 无引用 |
| `picobench/` | picobench | 2.9.0 | 单头 | 无引用；与 Catch2 BENCHMARK 职责重叠 |
| `plf_nanotimer/` | plf::nanotimer | 未标注 | 单头 | 无引用 |
| `SafeInt/` | Microsoft SafeInt | 3.0.29p | 三头 | 无引用 |
| `simdb/` | SimDB 共享内存 KV | 未标注 | 单头 | 无引用；项目约束禁用数据库，启用需先修订约束 |
| `termcolor/` | termcolor | 未标注 | 单头 | 无引用；spdlog 已带着色能力 |
| `tinyutf8/` | tinyutf8 | 未标注 | 单头 | 无引用 |
| `tinyxml2/` | TinyXML-2 | 11.0.0 | 源码对（.h/.cpp） | 无引用 |
| `WinToast/` | WinToast（Windows 通知） | 未标注 | 源码对 + docs/ | 无引用 |
| `croncpp.h` | croncpp（cron 表达式） | 未标注 | 顶层单头 | 无引用 |
| `dylib.hpp` | dylib（动态库加载） | 3.0.1 | 顶层单头 | 无引用 |

另：`nlohmann/readme.txt` 为随库入库的上游说明文件（所在目录已跟踪）。

## 7. 目录清单

```
third_party/
├─ catch2/          [白名单] Catch2 v3.16.1 单元测试框架（编译库）
├─ cpp-httplib/     [白名单] HTTP server/client 单头库 0.66.0
├─ cxxopts/         [白名单] 命令行参数解析单头库 3.3.1（2026-11-21 登记升级）
├─ nlohmann/        [白名单] JSON 单头库 3.12.0（json.hpp / json_fwd.hpp）
├─ rapidcsv/        [白名单] CSV 解析单头库 9.09
├─ spdlog/          [白名单] 日志库 1.17.0（编译库 + bundled fmt）
├─ toml/            [白名单] toml++ 单头库 3.4.0
├─ README.md        本文件（各库子目录内的 README.md 为上游原文/登记说明）
└─ （29 项备用库区条目见第 6 节清单：已入库、零引用、启用须先登记）
```

各库 README.md 分布：boolinq、catch2、cereal、cpp-httplib、cxxopts、hash-library（readme.md）、nlohmann、plf_nanotimer、rapidcsv、simdb、spdlog、toml、WinToast 拥有 README.md；date、exprtk、ghc、inja、magic_enum、termcolor 仅有上游 readme.txt；其余备用库条目无 readme。

## 8. 盘点方法（可重放）

```powershell
# 跟踪范围
git ls-files third_party
# 工作区差异（F5 违规检测）
git status --short -- third_party
git diff --stat -- third_party
# 版本核对（示例）
Select-String -Path third_party/cxxopts/cxxopts.hpp -Pattern 'CXXOPTS__VERSION_'
# 引用核对（示例）
git grep -n "tomlplusplus\|concurrentqueue\|exprtk" -- ':!third_party' ':!build*'
```

权威出处：依赖白名单与选型理由见 `docs/06-技术架构文档.md` 第 5 节与附录 A（约束 2 含备用库区条款）；各库的核心功能/适用场景/选型依据详见 **`docs/07-第三方库选型指南.md`**；本文件为 third_party 目录的状态盘点、使用说明与禁改规则索引。
