add_subdirectory(mq)

add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE agv_logger systemd rt)

add_executable(inject_log inject_log.cpp)
target_link_libraries(inject_log PRIVATE agv_logger systemd rt)

add_executable(test_secureExit test_secureExit.cpp)
target_link_libraries(test_secureExit PRIVATE agv_ipc systemd rt)