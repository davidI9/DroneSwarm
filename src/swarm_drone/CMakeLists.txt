cmake_minimum_required(VERSION 3.20)
project(swarm_drone)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Cada tick recorre todas las muestras para cada vecino: sin optimizar, el
# dron y los tests van muy lentos.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(swarm_interfaces REQUIRED)

# Lógica del dron, sin dependencias de ROS.
add_library(drone_controller src/drone_controller.cpp src/domain.cpp)
target_include_directories(drone_controller PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Capa fina de ROS.
add_executable(drone src/drone_node.cpp)
target_link_libraries(drone
  drone_controller
  rclcpp::rclcpp
  ${geometry_msgs_TARGETS}
  ${swarm_interfaces_TARGETS})

install(TARGETS drone DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_drone_controller test/test_drone_controller.cpp)
  target_link_libraries(test_drone_controller drone_controller)
  target_include_directories(test_drone_controller PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()

ament_package()
