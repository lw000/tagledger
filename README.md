# tagledger — 工业点表（Tag Ledger）管理 HTTP 服务

基于 C++17 与 `third_party` 现有开源库（cpp-httplib 0.66.0、nlohmann/json 3.12.0、rapidcsv 9.09、Catch2 3.16、cxxopts 2.2.0、toml++ 3.4.0、spdlog 1.17.0）实现的 HTTP 服务，为工业采集点表（tag ledger）提供维护能力：REST API、白名单访问控制与原生 Web 前端；点表以 CSV 文件持久化。原名 collector，2026-11-15 经人工确认更名为 tagledger（体现"点表台账"的核心定位）。

## 功能

- **接口能力**：单读 / 单写 / 批量读 / 批量写（`/api/v1`，JSON 包络，完整请求/响应结构见 `docs/02-方案设计.md` 第 3 节）。
- **访问控制**：白名单中间件（来源 IP 精确 / CIDR 网段 / Origin 前缀通配），未命中 403 拦截并记审计日志（`config/whitelist.conf`）。
- **前端页面**：`web/index.html`，纯原生 HTML/CSS/JS，工业深色低眩光风格；支持新建 CSV 文件、添加点位写入、选中行编辑保存。
- **CSV 存储**：6 列标准表头（`name,io_address,address_type,point_type,rw,desc`），UTF-8 带 BOM（Excel 可直接打开）；文件名 `[A-Za-z0-9_-]{1,64}`；按文件 `shared_mutex` 读写锁 + 临时文件原子替换，批量写整批事务。

## 构建（C++17）

```powershell
# 一键脚本：MinGW（输出 build\）与 Visual Studio（输出 build-vs\<Debug|Release>\）
.\cmake-debug.bat            # MinGW Debug（run_tests.py 依赖此目录）
.\cmake-release.bat          # MinGW Release（可选 [jobs] 参数）
.\vs-debug.bat               # Visual Studio (MSVC) Debug
.\vs-release.bat             # Visual Studio (MSVC) Release
```

要求 GCC ≥ 8 或 MSVC（支持 C++17）；`third_party` 位于项目根目录，双工具链（MinGW / Visual Studio 2019+）均可构建。VS 脚本使用 "Visual Studio 16 2019" 生成器 + MSBuild 并行编译（需 CMake ≥ 3.14 且已安装 VS 2019 或更高版本）。

本机工具链（2026-11-15 起）：MinGW GCC 16.2.0（`C:\mingw64`）+ CMake 4.5.0；原 GCC 8.1 时代的 `std::filesystem` 禁令已解除（详见 `docs/03-编码实现说明.md` 2.1）。

## 目录结构

```
tagledger/
├─ apps/tagledger/             可执行入口（main.cpp：解析参数→装配→启停编排）
├─ services/
│  ├─ http_server/            HTTP 服务（HttpServer 类：内部白名单 + 请求处理器 + 启停）
│  └─ csv_store/              点位存储（CSV 文件管理与行读写）
├─ common/                    公共层（util 日志与文件操作、app_config、shutdown）
├─ tests/                     单元/性能/集成测试（与 services 镜像）
├─ web/  config/  docs/  third_party/
└─ CMakeLists.txt             顶层（Catch2 静态库 + add_subdirectory 分层构建）
```

## 运行

```powershell
.\build\tagledger.exe --port=8080 --data-dir=data --web-dir=web --config-dir=config
# 浏览器打开 http://localhost:8080/
```

缺省白名单放行 `127.0.0.1`、`::1`、`192.168.0.0/16` 与 `origin:http://localhost:*`；删除配置文件则进入"放行所有来源"的开放模式（启动日志会 WARN 提示）。

## 测试

```powershell
python tests\scripts\run_tests.py          # 全量：Catch2 单元/退出/性能 + Python 集成脚本
python tests\scripts\run_tests.py --skip-perf
.\build\tagledger_tests.exe "[unit]"       # 仅单元测试（另可 "[shutdown]"、"[perf]"）
python tests\scripts\smoke_api.py          # API 冒烟（需服务运行中，--base-url 可指定）
python tests\scripts\graceful_shutdown_test.py   # 安全退出集成（自管服务生命周期）
```

测试框架为 `third_party/catch2`（Catch2 v3.16 编译库）；集成脚本为 Python 标准库实现，跨平台。规范详见 `docs/05-测试与安全退出规范.md`。

## 优雅退出

服务捕获 `SIGINT/SIGTERM`（Windows 另含 `SIGBREAK`）并暴露 `POST /api/admin/shutdown`（白名单保护）。退出顺序：停止接入 → 排空并完成在途请求（httplib 线程池 join）→ 存储析构 → 日志 flush；CSV 写为临时文件 + 原子替换，强杀也不损坏旧数据。

## 文档

| 文档 | 内容 |
| --- | --- |
| `docs/01-需求分析.md` | 背景、功能/非功能需求、验收标准 |
| `docs/02-方案设计.md` | 选库依据、总体架构、接口定义、白名单规则、CSV 存储与并发设计 |
| `docs/03-编码实现说明.md` | 代码组织、工具链兼容决策、关键实现细节 |
| `docs/04-测试验证报告.md` | 构建/单元/冒烟/白名单/并发验证结论与已知限制 |
| `docs/05-测试与安全退出规范.md` | Catch2 测试规范、目录/命名/分组、跨平台配置、安全退出机制与验证要点 |
| `docs/06-技术架构文档.md` | 系统结构、模块职责、依赖与交互、技术选型、数据流向与部署形态（附架构图） |
| `CODEBUDDY.md` | AI 开发规范（通用版，CodeBuddy 插件自动加载）：编码风格（C 系列）、方案确认流程（R 系列）、AI 协作要点（P 系列）、模块化规范（S/B/F 系列）与强制流程；项目专属信息索引指向 docs/06 |
