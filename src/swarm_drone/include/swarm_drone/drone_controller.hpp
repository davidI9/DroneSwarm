#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

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
  // Tasa α del descenso de gradiente hacia el centroide.
  double alpha = 0.5;
  // Velocidad máxima: ningún paso supera v_max · dt.
  double v_max_mps = 1.5;
  // Distancia entre muestras al densificar el dominio Ω.
  double sample_spacing_m = 0.02;
  // Separación mínima entre dos drones: la capa de seguridad encoge la celda
  // de Voronoi del espacio completo r_safe / 2 a cada lado del bisector.
  double r_safe_m = 0.3;
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
  Vec3 centroid_or_nearest_sample() const;
  Vec3 apply_safety_layer(Vec3 step) const;
  bool owns(const Vec3 & sample) const;

  DroneConfig config_;
  DroneState state_;
  // Vacío hasta que llega la primera mano; después se conserva la última.
  std::vector<Vec3> domain_;
  // Última posición conocida de cada vecino. Ordenado por id para que el
  // recorrido sea determinista.
  std::map<DroneId, Vec3> neighbors_;
  std::optional<double> last_tick_s_;
};

}  // namespace swarm_drone
