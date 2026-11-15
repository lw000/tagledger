# common/ —— 公共工具与横切层（F4 末端：依赖 common → third_party，永不 include services/apps）
add_library(tagledger_common STATIC
    app_config.cpp
    logger.cpp
    shutdown.cpp
    util.cpp
)

# 包含根 = 项目根：消费者以 "common/<file>.h" 引用（F3）
target_include_directories(tagledger_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# app_config.cpp 使用 toml++（配置解析强制，硬性约束 13）；logger.cpp 使用 spdlog（日志库，
# 白名单第 7 库）；均为实现依赖，不进公开头
target_include_directories(tagledger_common PRIVATE
    "${THIRD_PARTY_DIR}/toml"
)
target_link_libraries(tagledger_common PRIVATE spdlog_lib)

target_compile_options(tagledger_common PRIVATE ${COLLECTOR_WARN_FLAGS})
