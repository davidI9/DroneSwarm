#include "swarm_drone/drone_controller.hpp"

#include <cmath>
#include <numbers>

namespace swarm_drone
{

namespace
{
constexpr int kSpawnsPerRing = 16;
constexpr double kFirstRingRadius_m = 5.0;
constexpr double kRingSpacing_m = 1.5;
constexpr double kSpawnHeight_m = 0.5;

// Spawn por defecto: anillos concéntricos de 16 posiciones alrededor del
// origen. Dos ids distintos nunca comparten spawn por defecto.
Vec3 default_spawn(DroneId id)
{
  const auto ring = id / kSpawnsPerRing;
  const auto slot = id % kSpawnsPerRing;
  const double radius = kFirstRingRadius_m + kRingSpacing_m * ring;
  const double angle = 2.0 * std::numbers::pi * slot / kSpawnsPerRing;
  return {radius * std::cos(angle), radius * std::sin(angle), kSpawnHeight_m};
}

}  // namespace

DroneController::DroneController(const DroneConfig & config, const InstanceUuid & instance_uuid)
{
  state_.drone_id = config.drone_id;
  state_.instance_uuid = instance_uuid;
  state_.position = config.spawn.value_or(default_spawn(config.drone_id));
}

void DroneController::on_hand(const Landmarks &)
{
}

void DroneController::on_neighbor_state(const DroneState &, double)
{
}

DroneState DroneController::tick(double)
{
  // Invariante 9: sin mano el dron se queda quieto en su spawn. Este esqueleto
  // todavía no sabe moverse, así que también se queda quieto con mano.
  return state_;
}

}  // namespace swarm_drone
