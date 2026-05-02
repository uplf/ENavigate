find_package(PahoMqttCpp REQUIRED)

add_executable(mqtt_publisher mqtt_publisher.cpp)
target_link_libraries(mqtt_publisher PRIVATE -lmosquitto -lrt -lpthread)

add_executable(mqtt_publisher mqtt_publisher.cpp)
target_link_libraries(mqtt_publisher PRIVATE -lmosquitto -lrt -lpthread)
