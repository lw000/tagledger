# services/csv_store —— 点位存储服务（S2：目录名 = target 名）
add_library(csv_store STATIC
    src/csv_store.cpp
)

# 公开头包含根：消费者以 "csv_store/csv_store.h" 引用（F3）
target_include_directories(csv_store PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(csv_store PUBLIC tagledger_common)

# rapidcsv（CSV 解析）仅实现依赖，不进公开头
target_include_directories(csv_store PRIVATE
    "${THIRD_PARTY_DIR}/rapidcsv"
)

target_compile_options(csv_store PRIVATE ${COLLECTOR_WARN_FLAGS})
