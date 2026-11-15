# services/http_server —— HTTP 服务（HttpServer 类：内部白名单 + 请求处理器 + 启停）
add_library(http_server STATIC
    src/http_server.cpp
    src/routes.cpp
    src/whitelist.cpp
)

# 公开头包含根：消费者以 "http_server/<file>.h" 引用（F3）
target_include_directories(http_server PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# 公开头以 pimpl 隔离 httplib/json，但私有头（src/http_server_internal.h）需要其包含根
target_include_directories(http_server PRIVATE
    "${THIRD_PARTY_DIR}/cpp-httplib"
    "${THIRD_PARTY_DIR}"
)

target_link_libraries(http_server
    PUBLIC csv_store tagledger_common
    PRIVATE ws2_32   # httplib（WinSock）
)

target_compile_options(http_server PRIVATE ${COLLECTOR_WARN_FLAGS})
