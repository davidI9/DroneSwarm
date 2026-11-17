#include "swarm_drone/drone_controller.hpp"

#include <algorithm>
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
// Desplazamiento del spawn que rompe la simetría entre drones solapados.
constexpr double kSymmetryBreak_m = 0.05;
// Pasadas de deslizamiento sobre los planos de la capa de seguridad antes del
// recorte final.
constexpr int kSlidePasses = 3;

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

// Dirección unitaria de la espiral áurea sobre la esfera para un drone_id:
// el ángulo avanza el ángulo áureo con cada id y la altura recorre [-1, 1]
// con una secuencia independiente (paso √2). Como √2 es irracional, dos ids
// distintos nunca comparten dirección.
Vec3 golden_direction(DroneId drone_id)
{
  const double fraction = std::fmod((drone_id + 0.5) * std::numbers::sqrt2, 1.0);
  const double z = 1.0 - 2.0 * fraction;
  const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
  const double golden_angle = 2.0 * std::numbers::pi / (std::numbers::phi * std::numbers::phi);
  const double angle = golden_angle * drone_id;
  return {radius * std::cos(angle), radius * std::sin(angle), z};
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
  if (!(config.r_safe_m >= 0.0)) {throw std::invalid_argument("r_safe debe ser >= 0");}
  state_.drone_id = config.drone_id;
  state_.instance_uuid = instance_uuid;
  // Invariante 3: ruptura de simetría. Dos drones que nacen en el mismo punto
  // quedan desplazados en direcciones distintas.
  state_.position = config.spawn.value_or(default_spawn(config.drone_id)) +
    golden_direction(config.drone_id) * kSymmetryBreak_m;
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
  // La capa de seguridad solo acorta el paso: sigue sin superar v_max · dt.
  step = apply_safety_layer(step);

  state_.position = state_.position + step;
  state_.velocity = step / dt;
  return state_;
}

// Invariante 4: el paso no sale de la celda de Voronoi del espacio completo
// encogida r_safe / 2 a cada lado de cada bisector. Si el vecino aplica la
// misma regla con las mismas posiciones, los dos no bajan de r_safe; si ya
// estaban más cerca, su distancia no disminuye.
Vec3 DroneController::apply_safety_layer(Vec3 step) const
{
  struct Plane
  {
    Vec3 normal;  // Unitaria, hacia el vecino.
    double limit;  // Lo máximo que el paso puede avanzar hacia el vecino.
  };
  std::vector<Plane> planes;
  planes.reserve(neighbors_.size());
  const Vec3 & own = state_.position;
  for (const auto & [id, position] : neighbors_) {
    const Vec3 offset = position - own;
    const double distance = norm(offset);
    Vec3 normal;
    if (distance > 0.0) {
      normal = offset / distance;
    } else {
      // Solapados del todo: dirección determinista y opuesta para cada uno.
      const Vec3 apart = golden_direction(id) - golden_direction(state_.drone_id);
      normal = apart / norm(apart);
    }
    // En invasión (distance < r_safe) el límite es 0: solo puede alejarse.
    planes.push_back({normal, std::max(0.0, (distance - config_.r_safe_m) / 2.0)});
  }

  // Deslizar a lo largo de los planos que el paso cruza, para no quedarse
  // parado contra un vecino cuando hay hueco a un lado.
  for (int pass = 0; pass < kSlidePasses; ++pass) {
    for (const auto & plane : planes) {
      const double excess = dot(step, plane.normal) - plane.limit;
      if (excess > 0.0) {step = step - plane.normal * excess;}
    }
  }
  // Deslizar sobre un plano puede volver a cruzar otro: el recorte final en
  // la dirección del paso garantiza todos a la vez (el paso nulo siempre
  // los cumple).
  double scale = 1.0;
  for (const auto & plane : planes) {
    const double along = dot(step, plane.normal);
    if (along > plane.limit) {scale = std::min(scale, plane.limit / along);}
  }
  return step * scale;
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
