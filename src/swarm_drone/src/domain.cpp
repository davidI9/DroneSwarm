#include "domain.hpp"

#include <algorithm>
#include <cmath>

#include "vec3_ops.hpp"

namespace swarm_drone
{

namespace
{

// Huesos de la topología de MediaPipe que quedan fuera de la palma. Los que
// unen dos vértices de la palma ya los cubre su relleno.
constexpr int kFingerBones[][2] = {
  {1, 2}, {2, 3}, {3, 4},          // pulgar
  {5, 6}, {6, 7}, {7, 8},          // índice
  {9, 10}, {10, 11}, {11, 12},     // corazón
  {13, 14}, {14, 15}, {15, 16},    // anular
  {17, 18}, {18, 19}, {19, 20},    // meñique
};

// Contorno de la palma; se rellena en abanico desde la muñeca (landmark 0).
constexpr int kWrist = 0;
constexpr int kPalmRim[] = {1, 5, 9, 13, 17};

// Tolerancia para aceptar puntos de la rejilla justo en el borde de un
// triángulo (en coordenadas baricéntricas).
constexpr double kEdgeTolerance = 1e-9;

// Muestras en el centro de cada uno de los n tramos iguales del hueso: así
// los extremos compartidos entre huesos no se duplican.
void sample_bone(const Vec3 & a, const Vec3 & b, double spacing, std::vector<Vec3> & out)
{
  const double length = norm(b - a);
  const int n = std::max(1, static_cast<int>(std::ceil(length / spacing)));
  for (int i = 0; i < n; ++i) {
    out.push_back(a + (b - a) * ((i + 0.5) / n));
  }
}

// Rellena la palma con una única rejilla cuadrada de lado `spacing` sobre el
// plano de la palma, con origen en la muñeca. Cada punto de la rejilla se
// asigna al primer triángulo del abanico que lo contiene (en proyección) y se
// lleva a la superficie de ese triángulo. Al compartir rejilla, la densidad es
// la misma a ambos lados de las diagonales del abanico y no hay duplicados.
void sample_palm(const Landmarks & hand, double spacing, std::vector<Vec3> & out)
{
  const Vec3 & wrist = hand[kWrist];
  const Vec3 normal_raw = cross(hand[kPalmRim[1]] - wrist, hand[kPalmRim[4]] - wrist);
  const Vec3 u_raw = hand[kPalmRim[2]] - wrist;
  if (norm(normal_raw) == 0.0 || norm(u_raw) == 0.0) {return;}
  const Vec3 normal = normal_raw / norm(normal_raw);
  const Vec3 u_in_plane = u_raw - normal * dot(u_raw, normal);
  if (norm(u_in_plane) == 0.0) {return;}
  const Vec3 u = u_in_plane / norm(u_in_plane);
  const Vec3 v = cross(normal, u);

  struct Point2 {double u, v;};
  const auto project = [&](const Vec3 & p) {
      return Point2{dot(p - wrist, u), dot(p - wrist, v)};
    };

  constexpr std::size_t kRimSize = std::size(kPalmRim);
  Point2 rim[kRimSize];
  double u_min = 0.0, u_max = 0.0, v_min = 0.0, v_max = 0.0;
  for (std::size_t k = 0; k < kRimSize; ++k) {
    rim[k] = project(hand[kPalmRim[k]]);
    u_min = std::min(u_min, rim[k].u);
    u_max = std::max(u_max, rim[k].u);
    v_min = std::min(v_min, rim[k].v);
    v_max = std::max(v_max, rim[k].v);
  }

  const int i_min = static_cast<int>(std::floor(u_min / spacing));
  const int i_max = static_cast<int>(std::ceil(u_max / spacing));
  const int j_min = static_cast<int>(std::floor(v_min / spacing));
  const int j_max = static_cast<int>(std::ceil(v_max / spacing));
  for (int j = j_min; j <= j_max; ++j) {
    for (int i = i_min; i <= i_max; ++i) {
      const Point2 p{i * spacing, j * spacing};
      for (std::size_t k = 0; k + 1 < kRimSize; ++k) {
        // Coordenadas baricéntricas de p en el triángulo (muñeca, b, c) proyectado.
        const Point2 b = rim[k];
        const Point2 c = rim[k + 1];
        const double det = b.u * c.v - c.u * b.v;
        if (det == 0.0) {continue;}
        const double wb = (p.u * c.v - c.u * p.v) / det;
        const double wc = (b.u * p.v - p.u * b.v) / det;
        const double wa = 1.0 - wb - wc;
        if (wa >= -kEdgeTolerance && wb >= -kEdgeTolerance && wc >= -kEdgeTolerance) {
          out.push_back(
            wrist * wa + hand[kPalmRim[k]] * wb + hand[kPalmRim[k + 1]] * wc);
          break;
        }
      }
    }
  }
}

}  // namespace

std::vector<Vec3> densify_hand(const Landmarks & hand, double sample_spacing_m)
{
  std::vector<Vec3> samples;
  for (const auto & [a, b] : kFingerBones) {
    sample_bone(hand[a], hand[b], sample_spacing_m, samples);
  }
  sample_palm(hand, sample_spacing_m, samples);
  return samples;
}

}  // namespace swarm_drone
