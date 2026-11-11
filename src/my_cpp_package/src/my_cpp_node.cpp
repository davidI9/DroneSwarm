/*// src/nodo_coordenadas.cpp
#include <memory>
#include <chrono>
#include "rclcpp/rclcpp.hpp"
#include "drone.hpp"
#include <cmath>

// Header generado automáticamente a partir de SpecialCoords.msg (CamelCase -> snake_case)
#include "my_cpp_package/msg/special_coords.hpp" 
#include "my_position.hpp"

using namespace std::chrono_literals;

class NodoDrone : public rclcpp::Node {

  private:
    void publicar_estado() {
      // 1. Modificar el estado usando la clase C++
      drone_pos_.mover(0.5, 1.2, 0.2);

      // 2. Instanciar y rellenar el mensaje de ROS 2
      auto msg = my_cpp_package::msg::SpecialCoords();
      msg.x = drone_pos_.getX();
      msg.y = drone_pos_.getY();
      msg.z = drone_pos_.getZ();
      msg.id = drone_id_;

      RCLCPP_INFO(this->get_logger(), "[%d] Posicion actual: x=%.2f, y=%.2f, z=%.2f", 
                  msg.id, msg.x, msg.y, msg.z);

      // 3. Publicar por DDS
      publisher_->publish(msg);
    }

    int drone_id_;
    Drone drone_pos_; // Instancia de la clase C++ pura
    rclcpp::Publisher<my_cpp_package::msg::SpecialCoords>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;

  public:

    NodoDrone() : Node("nodo_coordenadas_custom") {

      this->declare_parameter<int>("drone_id", 1); // Declara 'drone_id' con valor por defecto 1
      drone_id_ = this->get_parameter("drone_id").as_int();

      double angle = drone_id_ * (2.0 * M_PI / 10.0); // Repartidos en círculo
      double radius = 3.0;
      drone_pos_ = Drone(radius * std::cos(angle), radius * std::sin(angle), 1.0);

      // Publisher del tipo personalizado
      publisher_ = this->create_publisher<my_cpp_package::msg::SpecialCoords>("topic_drone", 10);
    
      // Temporizador
      timer_ = this->create_wall_timer(
        500ms, std::bind(&NodoDrone::publicar_estado, this));
    }
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NodoDrone>());
  rclcpp::shutdown();
  return 0;
}
*/


#include <memory>
#include <chrono>
#include "rclcpp/rclcpp.hpp"
#include "drone.hpp"
#include <cmath>

#include "my_cpp_package/msg/special_coords.hpp" 
#include "visualization_msgs/msg/marker.hpp" // Header para RViz2

using namespace std::chrono_literals;

class NodoDrone : public rclcpp::Node {
private:
  void publicar_estado() {
    // 1. Actualizar estado
    drone_pos_.mover(0.5, 1.2, 0.2);

    // 2. Publicar mensaje personalizado (para la red/otros nodos)
    auto msg = my_cpp_package::msg::SpecialCoords();
    msg.id = drone_id_;
    msg.x = drone_pos_.getX();
    msg.y = drone_pos_.getY();
    msg.z = drone_pos_.getZ();
    publisher_->publish(msg);

    // 3. Publicar marcador 3D (para RViz2)
    visualization_msgs::msg::Marker marker;
    marker.header.frame_id = "map"; // Marco de referencia universal
    marker.header.stamp = this->now();
    marker.ns = "drones";
    marker.id = drone_id_; // Permite a RViz2 actualizar la posición del dron ID 1
    marker.type = visualization_msgs::msg::Marker::SPHERE;
    marker.action = visualization_msgs::msg::Marker::ADD;

    marker.pose.position.x = drone_pos_.getX();
    marker.pose.position.y = drone_pos_.getY();
    marker.pose.position.z = drone_pos_.getZ();

    // Tamaño de la esfera en metros (0.4m x 0.4m x 0.4m)
    marker.scale.x = 0.4;
    marker.scale.y = 0.4;
    marker.scale.z = 0.4;

    // Color verde brillante
    marker.color.a = 1.0f; // Opacidad total
    marker.color.r = 0.0f;
    marker.color.g = 1.0f;
    marker.color.b = 0.0f;

    marker_pub_->publish(marker);

    RCLCPP_INFO(this->get_logger(), "[Dron %d] Posicion: x=%.2f, y=%.2f, z=%.2f", 
                drone_id_, msg.x, msg.y, msg.z);
  }

  int drone_id_;
  Drone drone_pos_; 
  rclcpp::Publisher<my_cpp_package::msg::SpecialCoords>::SharedPtr publisher_;
  rclcpp::Publisher<visualization_msgs::msg::Marker>::SharedPtr marker_pub_; // Publicador para RViz2
  rclcpp::TimerBase::SharedPtr timer_;

public:
  NodoDrone() : Node("nodo_coordenadas_custom") {
    this->declare_parameter<int>("drone_id", 1);
    drone_id_ = this->get_parameter("drone_id").as_int();

    double angle = drone_id_ * (2.0 * M_PI / 10.0);
    double radius = 3.0;
    drone_pos_ = Drone(radius * std::cos(angle), radius * std::sin(angle), 1.0);

    publisher_ = this->create_publisher<my_cpp_package::msg::SpecialCoords>("topic_drone", 10);
    marker_pub_ = this->create_publisher<visualization_msgs::msg::Marker>("topic_drone_marker", 10);
  
    timer_ = this->create_wall_timer(
      500ms, std::bind(&NodoDrone::publicar_estado, this));
  }
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NodoDrone>());
  rclcpp::shutdown();
  return 0;
}