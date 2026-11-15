cmake_minimum_required(VERSION 3.12)
project(tagledger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# 统一告警/编译选项（各 target 经 ${COLLECTOR_WARN_FLAGS} 引用）
# /utf-8：源码为 UTF-8（含中文注释），MSVC 默认按系统代码页(936)误读会破坏代码行边界（C4819→C2065）
set(COLLECTOR_WARN_FLAGS
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

# MinGW 头文件默认 _WIN32_WINNT 过低，缺少 WSAPoll/inet_pton/CreateFile2 声明
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00 -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

# ---------- Catch2 v3 静态库（来自 third_party/catch2，v3.16 编译形态） ----------
# 头文件引用方式：<catch2/catch_test_macros.hpp> 等，包含根为 third_party。
# catch_user_config.hpp 为构建期生成文件（源自 catch_user_config.hpp.in 模板）。
set(CATCH_CONFIG_DEFAULT_REPORTER "console")
set(CATCH_CONFIG_CONSOLE_WIDTH 100)
set(CATCH2_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CATCH2_GENERATED_DIR}/catch2")
configure_file(
    "${THIRD_PARTY_DIR}/catch2/catch_user_config.hpp.in"
    "${CATCH2_GENERATED_DIR}/catch2/catch_user_config.hpp"
    @ONLY
)

file(GLOB_RECURSE CATCH2_SOURCES "${THIRD_PARTY_DIR}/catch2/*.cpp")
add_library(catch2_lib STATIC ${CATCH2_SOURCES})
target_include_directories(catch2_lib PUBLIC
    "${THIRD_PARTY_DIR}"
    "${CATCH2_GENERATED_DIR}"
)
# v3 自带 main（internal/catch_main.cpp），测试目标无需再写 main。
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(catch2_lib PUBLIC Threads::Threads)
endif()

# ---------- spdlog v1.17.0 静态库（编译形态，来自 third_party/spdlog；白名单第 7 库） ----------
# 头文件引用方式：<spdlog/spdlog.h> 等，包含根为 third_party/spdlog/include。
# 仅拷贝 include/ 与 src/（bundled fmt 随附），不套用项目告警旗标（第三方代码，与 catch2_lib 同策略）；
# MSVC 须 /utf-8：bundled fmt 检测到非 UTF-8 源编码时静态断言拒绝编译（同顶层 /utf-8 约束）。
file(GLOB_RECURSE SPDLOG_SOURCES "${THIRD_PARTY_DIR}/spdlog/src/*.cpp")
add_library(spdlog_lib STATIC ${SPDLOG_SOURCES})
target_include_directories(spdlog_lib PUBLIC
    "${THIRD_PARTY_DIR}/spdlog/include"
)
target_compile_definitions(spdlog_lib PRIVATE SPDLOG_COMPILED_LIB)
target_compile_options(spdlog_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

# 多配置生成器（Visual Studio）：统一 exe 输出到 build-vs/<Config>/，
# 与顶层测试目标一致；单配置生成器（MinGW）不受影响（run_tests.py 依赖 build/ 根路径）。
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif()

# ---------- 分层结构（S 系列）：apps → services → common → third_party（F4 单向） ----------
add_subdirectory(common)
add_subdirectory(services/csv_store)
add_subdirectory(services/http_server)
add_subdirectory(apps/tagledger)

# ---------- 测试目标：单元 + 安全退出 + 性能基准（Catch2 标签区分） ----------
add_executable(tagledger_tests
    tests/unit/test_whitelist.cpp
    tests/unit/test_csv_store.cpp
    tests/unit/test_shutdown.cpp
    tests/unit/test_app_config.cpp
    tests/unit/test_logger.cpp
    tests/perf/test_perf.cpp
)

target_link_libraries(tagledger_tests PRIVATE
    http_server csv_store tagledger_common catch2_lib
)

target_compile_options(tagledger_tests PRIVATE ${COLLECTOR_WARN_FLAGS})

enable_testing()
# 命名规则：tagledger_<分组>；分组用 Catch2 标签过滤（见 docs/05 第 2 节）
add_test(NAME tagledger_unit      COMMAND tagledger_tests "[unit]")
add_test(NAME tagledger_shutdown  COMMAND tagledger_tests "[shutdown]")
add_test(NAME tagledger_perf      COMMAND tagledger_tests "[perf]")
