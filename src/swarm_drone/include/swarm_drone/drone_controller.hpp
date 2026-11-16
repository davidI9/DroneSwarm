#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace swarm_drone
{

struct Vec3
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

using DroneId = std::uint32_t;
using InstanceUuid = std::array<std::uint8_t, 16>;

// Los 21 landmarks de la mano, en metros y en el frame del mundo.
using Landmarks = std::array<Vec3, 21>;

struct DroneState
{
  DroneId drone_id = 0;
  InstanceUuid instance_uuid{};
  Vec3 position;
  Vec3 velocity;
};

struct DroneConfig
{
  DroneId drone_id = 0;
  // Sin valor, el spawn por defecto se deriva del drone_id.
  std::optional<Vec3> spawn;
};

// Toda la lógica de un dron, sin dependencias de ROS. Los instantes son
// segundos de un reloj local y monótono; nunca el reloj de otra máquina.
class DroneController
{
public:
  DroneController(const DroneConfig & config, const InstanceUuid & instance_uuid);

  void on_hand(const Landmarks & hand);
  void on_neighbor_state(const DroneState & state, double received_at_s);

  // Avanza un paso de control y devuelve el nuevo estado propio.
  DroneState tick(double now_s);

  const DroneState & state() const {return state_;}

private:
  DroneState state_;
};

}  // namespace swarm_drone
