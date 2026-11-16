#include <gtest/gtest.h>

#include <cmath>

#include "fake_bus.hpp"
#include "test_hand.hpp"

using swarm_drone::DroneConfig;
using swarm_drone::Vec3;
using swarm_drone::testing::FakeBus;
using swarm_drone::testing::HandShape;
using swarm_drone::testing::distance;
using swarm_drone::testing::open_hand;

namespace
{

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

TEST(CeldaVacia, UnDronQueNaceLejosAcabaLlegandoALaMano)
{
  const auto hand = open_hand();
  FakeBus bus;
  // Tres drones ya sobre la mano se quedan todas las muestras: el dron 9, a
  // 15 m, nace con la celda vacía.
  for (swarm_drone::DroneId id : {1u, 2u, 3u}) {
    auto config = config_for(id);
    config.spawn = hand[4 * id];
    bus.add_drone(config);
  }
  auto far = config_for(9);
  far.spawn = Vec3{15.0, 0.0, 2.5};
  bus.add_drone(far);
  bus.publish_hand(hand);

  bus.run_for(30.0);

  EXPECT_LT(HandShape(hand).distance_to(bus.position_of(9)), 0.15);
}

TEST(Velocidad, NingunPasoSuperaVmaxPorDt)
{
  FakeBus bus;
  // Con α = 1 el paso proporcional pide llegar al objetivo de golpe.
  for (swarm_drone::DroneId id = 0; id < 12; ++id) {
    auto config = config_for(id);
    config.alpha = 1.0;
    config.v_max_mps = 1.5;
    bus.add_drone(config);
  }
  bus.publish_hand(open_hand());

  const double max_step = 1.5 * bus.tick_period_s();
  auto before = bus.positions();
  bool someone_moved = false;
  for (int tick = 0; tick < 200; ++tick) {
    bus.step();
    const auto after = bus.positions();
    for (std::size_t i = 0; i < after.size(); ++i) {
      const double step = distance(before[i], after[i]);
      EXPECT_LE(step, max_step * (1.0 + 1e-9)) << "dron " << i << " en el tick " << tick;
      someone_moved = someone_moved || step > 0.0;
    }
    before = after;
  }
  EXPECT_TRUE(someone_moved);
}

class Convergencia : public ::testing::TestWithParam<int> {};

TEST_P(Convergencia, LaEnergiaDejaDeBajarYTodosLosDronesQuedanSobreLaMano)
{
  const auto hand = open_hand();
  const HandShape shape(hand);
  FakeBus bus;
  const int swarm_size = GetParam();
  for (int id = 0; id < swarm_size; ++id) {bus.add_drone(config_for(id));}
  bus.publish_hand(hand);

  const double initial = shape.coverage_energy(bus.positions());
  bus.run_for(30.0);
  const double settling = shape.coverage_energy(bus.positions());
  bus.run_for(5.0);
  const double final = shape.coverage_energy(bus.positions());

  EXPECT_LT(final, initial / 10.0);
  EXPECT_LE(final, settling * 1.01);
  EXPECT_NEAR(final, settling, settling * 0.01) << "la energía sigue cambiando";
  // Repartirse la mano cubre mucho mejor que amontonarse en su centro.
  const double collapsed = shape.coverage_energy({shape.center()});
  EXPECT_LT(final, collapsed / 4.0);

  for (const auto & p : bus.positions()) {
    EXPECT_LT(shape.distance_to(p), 0.15) << "dron en (" << p.x << ", " << p.y << ", " << p.z << ")";
  }
}

// Menos y más drones que landmarks.
INSTANTIATE_TEST_SUITE_P(TamanoDelEnjambre, Convergencia, ::testing::Values(8, 40));

TEST(Dominio, ConLaMismaManoDosControladoresHacenLasMismasTrayectorias)
{
  const auto hand = open_hand();
  FakeBus bus_a;
  FakeBus bus_b;
  for (swarm_drone::DroneId id = 0; id < 6; ++id) {
    bus_a.add_drone(config_for(id));
    // En el bus B, cada dron ha visto antes otras manos: el dominio depende
    // solo de los últimos landmarks recibidos.
    auto & drone = bus_b.add_drone(config_for(id));
    drone.on_hand(swarm_drone::testing::shifted(hand, {0.0, 1.0, 0.5}));
    drone.on_hand(swarm_drone::testing::shifted(hand, {0.3, -2.0, 0.0}));
  }
  bus_a.publish_hand(hand);
  bus_b.publish_hand(hand);
  const auto start = bus_a.positions();

  for (int tick = 0; tick < 300; ++tick) {
    bus_a.step();
    bus_b.step();
    const auto a = bus_a.positions();
    const auto b = bus_b.positions();
    for (std::size_t i = 0; i < a.size(); ++i) {
      ASSERT_EQ(a[i].x, b[i].x) << "dron " << i << " en el tick " << tick;
      ASSERT_EQ(a[i].y, b[i].y) << "dron " << i << " en el tick " << tick;
      ASSERT_EQ(a[i].z, b[i].z) << "dron " << i << " en el tick " << tick;
    }
  }
  EXPECT_GT(distance(bus_a.positions()[0], start[0]), 1.0) << "los drones deberían haberse movido";
}
