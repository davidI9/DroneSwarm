#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "swarm_drone/drone_controller.hpp"

namespace swarm_drone::testing
{

// Los tests trabajan con una mano grande: con r_safe = 0.3 m, en la mano de
// 4 m de la mano falsa no caben 40 drones separados.
constexpr double kHandScale_m = 10.0;
constexpr double kHandHeight_m = 5.0;

// Mano abierta de la mano falsa del cliente (OPEN_HAND) convertida a metros
// con escala kHandScale_m y altura kHandHeight_m: x → Y, y → −Z y
// profundidad → X.
inline Landmarks open_hand()
{
  constexpr double kNormalized[21][3] = {
    {0.50, 0.85, 0.00},
    {0.42, 0.80, -0.02}, {0.36, 0.72, -0.03}, {0.32, 0.64, -0.04}, {0.29, 0.57, -0.05},
    {0.44, 0.58, -0.01}, {0.42, 0.46, -0.02}, {0.41, 0.38, -0.03}, {0.40, 0.31, -0.03},
    {0.50, 0.56, -0.01}, {0.50, 0.43, -0.02}, {0.50, 0.34, -0.03}, {0.50, 0.26, -0.03},
    {0.56, 0.58, -0.01}, {0.57, 0.46, -0.02}, {0.58, 0.38, -0.03}, {0.58, 0.31, -0.03},
    {0.61, 0.62, -0.01}, {0.63, 0.53, -0.02}, {0.64, 0.47, -0.03}, {0.65, 0.41, -0.03},
  };
  Landmarks hand;
  for (std::size_t i = 0; i < hand.size(); ++i) {
    const auto [x, y, z] = kNormalized[i];
    hand[i] = {
      z * kHandScale_m, (x - 0.5) * kHandScale_m, kHandHeight_m - (y - 0.5) * kHandScale_m};
  }
  return hand;
}

// La misma mano, trasladada: sirve para dar a un dron una mano distinta.
inline Landmarks shifted(Landmarks hand, const Vec3 & offset)
{
  for (auto & p : hand) {p = {p.x + offset.x, p.y + offset.y, p.z + offset.z};}
  return hand;
}

inline double distance(const Vec3 & a, const Vec3 & b)
{
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

// Referencia independiente de la forma de la mano para los tests: los dedos
// como segmentos y la palma (polígono 0-1-5-9-13-17) como triángulos,
// muestreados muy finos. No reutiliza la densificación del dron.
class HandShape
{
public:
  explicit HandShape(const Landmarks & hand)
  {
    constexpr double step_m = 0.01;
    const auto lerp = [](const Vec3 & a, const Vec3 & b, double t) {
        return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
      };
    constexpr int kFingerBones[][2] = {
      {1, 2}, {2, 3}, {3, 4}, {5, 6}, {6, 7}, {7, 8}, {9, 10}, {10, 11}, {11, 12},
      {13, 14}, {14, 15}, {15, 16}, {17, 18}, {18, 19}, {19, 20},
    };
    for (const auto & [a, b] : kFingerBones) {
      const int n = static_cast<int>(std::ceil(distance(hand[a], hand[b]) / step_m));
      for (int i = 0; i <= n; ++i) {points_.push_back(lerp(hand[a], hand[b], double(i) / n));}
    }
    constexpr int kPalm[] = {1, 5, 9, 13, 17};
    for (int k = 0; k + 1 < 5; ++k) {
      const Vec3 & a = hand[0];
      const Vec3 & b = hand[kPalm[k]];
      const Vec3 & c = hand[kPalm[k + 1]];
      const double longest = std::max(distance(a, b), distance(a, c));
      const int n = static_cast<int>(std::ceil(longest / step_m));
      for (int i = 0; i <= n; ++i) {
        for (int j = 0; i + j <= n; ++j) {
          const double s = double(i) / n;
          const double t = double(j) / n;
          points_.push_back({
              a.x + s * (b.x - a.x) + t * (c.x - a.x),
              a.y + s * (b.y - a.y) + t * (c.y - a.y),
              a.z + s * (b.z - a.z) + t * (c.z - a.z)});
        }
      }
    }
  }

  double distance_to(const Vec3 & p) const
  {
    double best = std::numeric_limits<double>::infinity();
    for (const auto & q : points_) {best = std::min(best, distance(p, q));}
    return best;
  }

  Vec3 center() const
  {
    Vec3 sum;
    for (const auto & q : points_) {sum = {sum.x + q.x, sum.y + q.y, sum.z + q.z};}
    const double n = static_cast<double>(points_.size());
    return {sum.x / n, sum.y / n, sum.z / n};
  }

  // Energía de cobertura: media de la distancia al cuadrado de cada punto de
  // la mano al dron más cercano.
  double coverage_energy(const std::vector<Vec3> & drones) const
  {
    double sum = 0.0;
    for (const auto & q : points_) {
      double best = std::numeric_limits<double>::infinity();
      for (const auto & d : drones) {
        const double dist = distance(d, q);
        best = std::min(best, dist * dist);
      }
      sum += best;
    }
    return sum / static_cast<double>(points_.size());
  }

private:
  std::vector<Vec3> points_;
};

}  // namespace swarm_drone::testing
