# lib/logger/CMakeLists.txt（纯头文件库，INTERFACE 目标）
add_library(agv_logger INTERFACE)
target_include_directories(agv_logger INTERFACE ${PROJECT_SOURCE_DIR}/include)

# src/log_daemon/CMakeLists.txt
add_executable(log_daemon main.cpp)
target_link_libraries(log_daemon PRIVATE agv_logger systemd rt)
# rt 提供 mq_open 等 POSIX MQ 函数
# systemd 提供 sd_journal_send