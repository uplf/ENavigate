find_package(nlohmann_json REQUIRED)

add_executable(Status Status.cpp)
target_link_libraries(Status PRIVATE nlohmann_json::nlohmann_json -lfcgi -lrt -lpthread agv_ipc systemd rt)
add_executable(Topo Topo.cpp)
target_link_libraries(Topo PRIVATE nlohmann_json::nlohmann_json -lfcgi -lrt -lpthread agv_ipc systemd rt)
add_executable(Task Task.cpp)
target_link_libraries(Task PRIVATE nlohmann_json::nlohmann_json -lfcgi -lrt -lpthread agv_ipc systemd rt)
