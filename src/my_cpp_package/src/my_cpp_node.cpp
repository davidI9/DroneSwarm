// src/nodo_coordenadas.cpp
#include <memory>
#include <chrono>
#include "rclcpp/rclcpp.hpp"

// Header generado automáticamente a partir de CoordenadaEspecial.msg
#include "my_cpp_package/msg/my_position.hpp" 
#include "my_position.hpp"

using namespace std::chrono_literals;

class NodoCoordenadas : public rclcpp::Node {
public:
  NodoCoordenadas() : Node("nodo_coordenadas_custom"), pos_(0.0, 0.0, "Sensor_Principal") {
    // Publisher del tipo personalizado
    publisher_ = this->create_publisher<mi_paquete::msg::CoordenadaEspecial>("topic_coordenadas", 10);

    // Temporizador
    timer_ = this->create_wall_timer(
      500ms, std::bind(&NodoCoordenadas::publicar_estado, this));
  }

private:
  void publicar_estado() {
    // 1. Modificar el estado usando la clase C++
    pos_.mover(0.5, 1.2);

    // 2. Instanciar y rellenar el mensaje de ROS 2
    auto msg = mi_paquete::msg::CoordenadaEspecial();
    msg.x = pos_.getX();
    msg.y = pos_.getY();
    msg.etiqueta = pos_.getTag();

    RCLCPP_INFO(this->get_logger(), "[%s] Posicion actual: x=%.2f, y=%.2f", 
                msg.tag.c_str(), msg.x, msg.y);

    // 3. Publicar por DDS
    publisher_->publish(msg);
  }

  MiPosicion pos_; // Instancia de la clase C++ pura
  rclcpp::Publisher<mi_paquete::msg::CoordenadaEspecial>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NodoCoordenadas>());
  rclcpp::shutdown();
  return 0;
}