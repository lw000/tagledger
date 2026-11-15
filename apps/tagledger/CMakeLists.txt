# apps/tagledger —— 可执行入口（B4：仅装配编排）
add_executable(tagledger
    main.cpp
)

target_link_libraries(tagledger
    PRIVATE http_server csv_store tagledger_common
)

# 命令行解析强制 cxxopts（硬性约束 13）
target_include_directories(tagledger PRIVATE
    "${THIRD_PARTY_DIR}/cxxopts"
)

target_compile_options(tagledger PRIVATE ${COLLECTOR_WARN_FLAGS})

# 构建后将 web/ 与 config/ 拷到可执行目录，便于直接运行
add_custom_command(TARGET tagledger POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../web" "$<TARGET_FILE_DIR:tagledger>/web"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../config" "$<TARGET_FILE_DIR:tagledger>/config"
    COMMENT "Copying web/ and config/ next to tagledger.exe"
)
