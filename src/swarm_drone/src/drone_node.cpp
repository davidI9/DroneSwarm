// Capa fina de ROS alrededor de DroneController: traduce mensajes y
// temporizadores. Toda la lógica del dron vive en el controlador.

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/vector3.hpp"
#include "rclcpp/rclcpp.hpp"
#include "swarm_drone/drone_controller.hpp"
#include "swarm_interfaces/msg/drone_state.hpp"
#include "swarm_interfaces/msg/hand_landmarks.hpp"
#include "visualization_msgs/msg/marker_array.hpp"

namespace swarm_drone
{

namespace
{

constexpr char kFrameId[] = "map";

// /swarm/state: baja latencia, sin acumular estados viejos.
const rclcpp::QoS kSwarmStateQos = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();

Vec3 to_vec3(const geometry_msgs::msg::Point & p) {return {p.x, p.y, p.z};}
Vec3 to_vec3(const geometry_msgs::msg::Vector3 & v) {return {v.x, v.y, v.z};}

geometry_msgs::msg::Point to_point(const Vec3 & v)
{
  geometry_msgs::msg::Point p;
  p.x = v.x;
  p.y = v.y;
  p.z = v.z;
  return p;
}

geometry_msgs::msg::Vector3 to_vector3(const Vec3 & v)
{
  geometry_msgs::msg::Vector3 out;
  out.x = v.x;
  out.y = v.y;
  out.z = v.z;
  return out;
}

InstanceUuid random_instance_uuid()
{
  std::random_device device;
  std::uniform_int_distribution<int> byte(0, 255);
  InstanceUuid uuid{};
  for (auto & b : uuid) {b = static_cast<std::uint8_t>(byte(device));}
  return uuid;
}

double steady_now_s()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// El nombre del nodo (drone_<id>) depende de un parámetro, y los parámetros
// solo se leen desde un nodo. Un nodo efímero, sin servicios ni rosout, lee
// drone_id de los argumentos --ros-args antes de crear el nodo real.
DroneId read_drone_id()
{
  auto options = rclcpp::NodeOptions()
    .start_parameter_services(false)
    .start_parameter_event_publisher(false)
    .enable_rosout(false);
  auto bootstrap = std::make_shared<rclcpp::Node>(
    "drone_bootstrap_" + std::to_string(getpid()), options);
  bootstrap->declare_parameter("drone_id", rclcpp::PARAMETER_INTEGER);
  const auto id = bootstrap->get_parameter("drone_id").as_int();
  if (id < 0) {throw std::invalid_argument("drone_id debe ser >= 0");}
  return static_cast<DroneId>(id);
}

}  // namespace

class DroneNode : public rclcpp::Node
{
public:
  explicit DroneNode(DroneId drone_id)
  : Node("drone_" + std::to_string(drone_id)),
    controller_(make_config(drone_id), random_instance_uuid())
  {
    const double rate_hz = declare_parameter("rate_hz", 20.0);

    state_pub_ = create_publisher<swarm_interfaces::msg::DroneState>(
      "/swarm/state", kSwarmStateQos);
    marker_pub_ = create_publisher<visualization_msgs::msg::MarkerArray>(
      "/swarm/markers", rclcpp::QoS(10));

    hand_sub_ = create_subscription<swarm_interfaces::msg::HandLandmarks>(
      "/hand/landmarks", rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local(),
      [this](const swarm_interfaces::msg::HandLandmarks & msg) {on_hand(msg);});
    state_sub_ = create_subscription<swarm_interfaces::msg::DroneState>(
      "/swarm/state", kSwarmStateQos,
      [this](const swarm_interfaces::msg::DroneState & msg) {on_neighbor_state(msg);});

    timer_ = create_wall_timer(
      std::chrono::duration<double>(1.0 / rate_hz), [this] {on_tick();});

    const auto & p = controller_.state().position;
    RCLCPP_INFO(get_logger(), "Dron %u listo en spawn (%.2f, %.2f, %.2f)",
      drone_id, p.x, p.y, p.z);
  }

private:
  DroneConfig make_config(DroneId drone_id)
  {
    DroneConfig config;
    config.drone_id = drone_id;
    config.alpha = declare_parameter("alpha", config.alpha);
    config.v_max_mps = declare_parameter("v_max", config.v_max_mps);
    config.sample_spacing_m = declare_parameter("sample_spacing", config.sample_spacing_m);
    config.r_safe_m = declare_parameter("r_safe", config.r_safe_m);
    const auto x = declare_parameter("spawn_x", rclcpp::PARAMETER_DOUBLE);
    const auto y = declare_parameter("spawn_y", rclcpp::PARAMETER_DOUBLE);
    const auto z = declare_parameter("spawn_z", rclcpp::PARAMETER_DOUBLE);
    const auto is_set = [](const rclcpp::ParameterValue & v) {
        return v.get_type() != rclcpp::PARAMETER_NOT_SET;
      };
    const int spawn_coords_set = is_set(x) + is_set(y) + is_set(z);
    if (spawn_coords_set == 3) {
      config.spawn = Vec3{x.get<double>(), y.get<double>(), z.get<double>()};
    } else if (spawn_coords_set != 0) {
      throw std::invalid_argument("spawn_x, spawn_y y spawn_z se fijan los tres juntos");
    }
    return config;
  }

  void on_hand(const swarm_interfaces::msg::HandLandmarks & msg)
  {
    Landmarks hand;
    for (std::size_t i = 0; i < hand.size(); ++i) {
      hand[i] = to_vec3(msg.landmarks[i]);
    }
    controller_.on_hand(hand);
  }

  void on_neighbor_state(const swarm_interfaces::msg::DroneState & msg)
  {
    DroneState state;
    state.drone_id = msg.drone_id;
    std::copy(msg.instance_uuid.begin(), msg.instance_uuid.end(), state.instance_uuid.begin());
    state.position = to_vec3(msg.position);
    state.velocity = to_vec3(msg.velocity);
    // Invariante 7: el tiempo de recepción es local, no el stamp del emisor.
    controller_.on_neighbor_state(state, steady_now_s());
  }

  void on_tick()
  {
    const auto state = controller_.tick(steady_now_s());
    const auto stamp = now();

    swarm_interfaces::msg::DroneState msg;
    msg.header.stamp = stamp;
    msg.header.frame_id = kFrameId;
    msg.drone_id = state.drone_id;
    std::copy(state.instance_uuid.begin(), state.instance_uuid.end(), msg.instance_uuid.begin());
    msg.position = to_point(state.position);
    msg.velocity = to_vector3(state.velocity);
    state_pub_->publish(msg);

    marker_pub_->publish(make_markers(state, stamp));
  }

  static visualization_msgs::msg::MarkerArray make_markers(
    const DroneState & state, const rclcpp::Time & stamp)
  {
    using visualization_msgs::msg::Marker;
    Marker sphere;
    sphere.header.frame_id = kFrameId;
    sphere.header.stamp = stamp;
    sphere.ns = "drones";
    sphere.id = static_cast<int>(state.drone_id);
    sphere.type = Marker::SPHERE;
    sphere.action = Marker::ADD;
    sphere.pose.position = to_point(state.position);
    sphere.pose.orientation.w = 1.0;
    sphere.scale.x = sphere.scale.y = sphere.scale.z = 0.3;
    sphere.color.r = 0.1f;
    sphere.color.g = 0.8f;
    sphere.color.b = 0.2f;
    sphere.color.a = 1.0f;
    // Si el dron cae, su esfera desaparece de RViz2 poco después.
    sphere.lifetime = rclcpp::Duration::from_seconds(1.0);

    Marker label = sphere;
    label.ns = "drone_ids";
    label.type = Marker::TEXT_VIEW_FACING;
    label.text = std::to_string(state.drone_id);
    label.pose.position.z += 0.35;
    label.scale.x = label.scale.y = 0.0;
    label.scale.z = 0.25;
    label.color.r = label.color.g = label.color.b = 1.0f;

    visualization_msgs::msg::MarkerArray markers;
    markers.markers = {sphere, label};
    return markers;
  }

  DroneController controller_;
  rclcpp::Publisher<swarm_interfaces::msg::DroneState>::SharedPtr state_pub_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr marker_pub_;
  rclcpp::Subscription<swarm_interfaces::msg::HandLandmarks>::SharedPtr hand_sub_;
  rclcpp::Subscription<swarm_interfaces::msg::DroneState>::SharedPtr state_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace swarm_drone

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  int exit_code = 0;
  try {
    const auto drone_id = swarm_drone::read_drone_id();
    rclcpp::spin(std::make_shared<swarm_drone::DroneNode>(drone_id));
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("drone"), "%s", e.what());
    exit_code = 1;
  }
  rclcpp::shutdown();
  return exit_code;
}
