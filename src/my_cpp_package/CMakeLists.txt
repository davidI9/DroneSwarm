cmake_minimum_required(VERSION 3.20)
project(my_cpp_package)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 1. Cargar dependencias de ROS 2 y generador de mensajes
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_ros REQUIRED)   # <-- AÑADIR ESTA LÍNEA
find_package(rclcpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)

# 2. Generar las cabeceras C++ a partir de tu archivo .msg
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/SpecialCoords.msg"
)

# 3. Declarar el ejecutable
add_executable(my_cpp_node src/my_cpp_node.cpp)

target_include_directories(my_cpp_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>)

target_compile_features(my_cpp_node PUBLIC c_std_17 cxx_std_20)

# 4. Vincular rclcpp y las interfaces del mensaje generado
ament_target_dependencies(my_cpp_node rclcpp)

rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(my_cpp_node "${cpp_typesupport_target}")

# 5. Instalar el ejecutable en la ruta del paquete
install(TARGETS my_cpp_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()