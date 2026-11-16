#include <gtest/gtest.h>

#include <cmath>

#include "fake_bus.hpp"

using swarm_drone::DroneConfig;
using swarm_drone::Vec3;
using swarm_drone::testing::FakeBus;

namespace
{

double distance(const Vec3 & a, const Vec3 & b)
{
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

DroneConfig config_for(swarm_drone::DroneId id)
{
  DroneConfig config;
  config.drone_id = id;
  return config;
}

}  // namespace

TEST(SinMano, NingunDronSeMueve)
{
  FakeBus bus;
  auto overlapped = config_for(2);
  overlapped.spawn = Vec3{1.0, 1.0, 1.0};
  auto also_overlapped = config_for(3);
  also_overlapped.spawn = Vec3{1.0, 1.0, 1.0};
  bus.add_drone(config_for(1));
  bus.add_drone(overlapped);
  bus.add_drone(also_overlapped);

  const Vec3 before[] = {bus.position_of(1), bus.position_of(2), bus.position_of(3)};
  bus.run_for(5.0);

  EXPECT_EQ(distance(bus.position_of(1), before[0]), 0.0);
  EXPECT_EQ(distance(bus.position_of(2), before[1]), 0.0);
  EXPECT_EQ(distance(bus.position_of(3), before[2]), 0.0);
}

TEST(Spawn, SinSpawnExplicitoCadaIdApareceEnUnSitioDistinto)
{
  FakeBus bus;
  for (swarm_drone::DroneId id = 0; id < 40; ++id) {bus.add_drone(config_for(id));}

  for (swarm_drone::DroneId a = 0; a < 40; ++a) {
    for (swarm_drone::DroneId b = a + 1; b < 40; ++b) {
      EXPECT_GT(distance(bus.position_of(a), bus.position_of(b)), 1.0)
        << "drones " << a << " y " << b;
    }
  }
}

TEST(Spawn, ElMismoIdApareceSiempreEnElMismoSitio)
{
  FakeBus bus_a;
  FakeBus bus_b;
  bus_a.add_drone(config_for(7));
  bus_b.add_drone(config_for(7));

  EXPECT_EQ(distance(bus_a.position_of(7), bus_b.position_of(7)), 0.0);
}

TEST(Spawn, ConSpawnExplicitoApareceDondeSeLeIndica)
{
  FakeBus bus;
  auto config = config_for(4);
  config.spawn = Vec3{-2.0, 3.5, 1.25};
  bus.add_drone(config);

  // Unos centímetros de margen: la ruptura de simetría desplaza el spawn.
  EXPECT_LT(distance(bus.position_of(4), Vec3{-2.0, 3.5, 1.25}), 0.1);
}
