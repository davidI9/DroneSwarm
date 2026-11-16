#pragma once

#include <cmath>

#include "swarm_drone/drone_controller.hpp"

namespace swarm_drone
{

inline Vec3 operator+(const Vec3 & a, const Vec3 & b) {return {a.x + b.x, a.y + b.y, a.z + b.z};}
inline Vec3 operator-(const Vec3 & a, const Vec3 & b) {return {a.x - b.x, a.y - b.y, a.z - b.z};}
inline Vec3 operator*(const Vec3 & a, double k) {return {a.x * k, a.y * k, a.z * k};}
inline Vec3 operator/(const Vec3 & a, double k) {return {a.x / k, a.y / k, a.z / k};}
inline double dot(const Vec3 & a, const Vec3 & b) {return a.x * b.x + a.y * b.y + a.z * b.z;}
inline Vec3 cross(const Vec3 & a, const Vec3 & b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double squared_norm(const Vec3 & a) {return dot(a, a);}
inline double norm(const Vec3 & a) {return std::sqrt(squared_norm(a));}

}  // namespace swarm_drone
