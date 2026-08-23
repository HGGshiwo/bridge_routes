cmake_minimum_required(VERSION 3.0.2)
project(bridge_routes)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/../.deps CACHE PATH "Directory for fetched content")
include(FetchContent)

FetchContent_Declare(paho_mqtt_cpp GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.cpp.git GIT_TAG v1.3.2 GIT_SHALLOW TRUE)
FetchContent_Declare(yaml-cpp GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git GIT_TAG 0.8.0 GIT_SHALLOW TRUE)

set(PAHO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(PAHO_WITH_MQTT_C ON CACHE BOOL "" FORCE)
set(PAHO_ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_DEB_PACKAGE OFF CACHE BOOL "" FORCE)

set(YAML_CPP_INSTALL OFF CACHE BOOL "Disable yaml-cpp install" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable yaml-cpp contrib" FORCE)


FetchContent_MakeAvailable(paho_mqtt_cpp yaml-cpp)

add_subdirectory(dk)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
dk_create_json_library(my_json_lib "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(catkin REQUIRED COMPONENTS 
    roscpp
    roslib
    rsos_msgs
    message_generation
)

add_service_files(
    FILES
    StringSrv.srv
)

generate_messages(
    DEPENDENCIES
    std_msgs
)

catkin_package(
    CATKIN_DEPENDS roscpp rsos_msgs message_runtime
)


add_compile_definitions(USE_ROS USE_ROS1)

include_directories(
    include
    ${catkin_INCLUDE_DIRS}
)

add_executable(${PROJECT_NAME}_node src/bridge_routes_node.cpp)
add_dependencies(${PROJECT_NAME}_node ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})

target_link_libraries(${PROJECT_NAME}_node
    dk    
    paho-mqttpp3-static
    my_json_lib
    yaml-cpp
    ${catkin_LIBRARIES}          
)
