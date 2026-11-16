#include "swarm_drone/drone_controller.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "domain.hpp"
#include "vec3_ops.hpp"

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
: config_(config)
{
  // Con α >= 2 el descenso de gradiente diverge (docs/teoria.md).
  if (!(config.alpha > 0.0 && config.alpha < 2.0)) {
    throw std::invalid_argument("alpha debe estar en (0, 2)");
  }
  if (!(config.v_max_mps > 0.0)) {throw std::invalid_argument("v_max debe ser > 0");}
  if (!(config.sample_spacing_m > 0.0)) {
    throw std::invalid_argument("sample_spacing debe ser > 0");
  }
  state_.drone_id = config.drone_id;
  state_.instance_uuid = instance_uuid;
  state_.position = config.spawn.value_or(default_spawn(config.drone_id));
}

void DroneController::on_hand(const Landmarks & hand)
{
  domain_ = densify_hand(hand, config_.sample_spacing_m);
}

void DroneController::on_neighbor_state(const DroneState & state, double)
{
  // El id duplicado se trata en otro ticket; mientras, se ignora.
  if (state.drone_id == state_.drone_id) {return;}
  neighbors_[state.drone_id] = state.position;
}

DroneState DroneController::tick(double now_s)
{
  const double dt = last_tick_s_ ? now_s - *last_tick_s_ : 0.0;
  last_tick_s_ = now_s;

  // Invariante 9: sin mano, el dron se queda quieto en su spawn.
  if (domain_.empty() || dt <= 0.0) {
    state_.velocity = {};
    return state_;
  }

  Vec3 step = (centroid_or_nearest_sample() - state_.position) * config_.alpha;
  const double max_step = config_.v_max_mps * dt;
  const double step_len = norm(step);
  if (step_len > max_step) {step = step * (max_step / step_len);}

  state_.position = state_.position + step;
  state_.velocity = step / dt;
  return state_;
}

// Invariante 2: una muestra pertenece al dron más cercano y, a igual
// distancia, al de drone_id menor.
bool DroneController::owns(const Vec3 & sample) const
{
  const double own = squared_norm(sample - state_.position);
  for (const auto & [id, position] : neighbors_) {
    const double other = squared_norm(sample - position);
    if (other < own || (other == own && id < state_.drone_id)) {return false;}
  }
  return true;
}

// Centroide de la celda o, si la celda está vacía, la muestra más cercana
// (invariante 5).
Vec3 DroneController::centroid_or_nearest_sample() const
{
  Vec3 sum;
  std::size_t count = 0;
  const Vec3 * nearest = nullptr;
  double nearest_d2 = std::numeric_limits<double>::infinity();
  for (const auto & sample : domain_) {
    if (owns(sample)) {
      sum = sum + sample;
      ++count;
    }
    const double d2 = squared_norm(sample - state_.position);
    if (d2 < nearest_d2) {
      nearest_d2 = d2;
      nearest = &sample;
    }
  }
  return count > 0 ? sum / static_cast<double>(count) : *nearest;
}

}  // namespace swarm_drone
