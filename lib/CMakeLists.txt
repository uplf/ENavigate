# logger 是纯头文件库，用 INTERFACE
add_library(agv_logger INTERFACE)
target_include_directories(agv_logger INTERFACE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ipc 有 .cpp 文件，编译成静态库
#add_library(agv_ipc STATIC
#    ipc/mq_wrapper.cpp
#    ipc/signalfd_helper.cpp
#    ipc/timerfd_helper.cpp
#)
#target_link_libraries(agv_ipc PUBLIC rt)  # rt 提供 mq_open