#pragma once

#include <vector>

#include "swarm_drone/drone_controller.hpp"

namespace swarm_drone
{

// Densifica los landmarks en el dominio Ω: muestras equiespaciadas sobre los
// huesos de los dedos y una rejilla sobre la palma rellena. Es determinista
// (sin aleatoriedad y con orden fijo): con los mismos landmarks, todos los
// drones obtienen exactamente las mismas muestras (invariante 1).
std::vector<Vec3> densify_hand(const Landmarks & hand, double sample_spacing_m);

}  // namespace swarm_drone
