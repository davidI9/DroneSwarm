#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "swarm_drone/drone_controller.hpp"

namespace swarm_drone::testing
{

// Sustituye a DDS en los tests: varios DroneController en el mismo proceso,
// un reloj simulado y entrega instantánea del estado de cada dron a los demás.
class FakeBus
{
public:
  explicit FakeBus(double tick_period_s = 0.05)
  : tick_period_s_(tick_period_s) {}

  DroneController & add_drone(const DroneConfig & config)
  {
    InstanceUuid uuid{};
    uuid[0] = static_cast<std::uint8_t>(next_instance_++);
    // Cada controlador es una instancia distinta, aunque repita drone_id.
    auto & drone = *drones_.emplace_back(std::make_unique<DroneController>(config, uuid));
    if (hand_) {drone.on_hand(*hand_);}
    return drone;
  }

  void publish_hand(const Landmarks & hand)
  {
    hand_ = hand;
    for (auto & drone : drones_) {drone->on_hand(hand);}
  }

  void step()
  {
    now_s_ += tick_period_s_;
    std::vector<DroneState> published;
    for (auto & drone : drones_) {published.push_back(drone->tick(now_s_));}
    for (std::size_t sender = 0; sender < drones_.size(); ++sender) {
      for (std::size_t receiver = 0; receiver < drones_.size(); ++receiver) {
        if (receiver != sender) {drones_[receiver]->on_neighbor_state(published[sender], now_s_);}
      }
    }
  }

  void run_for(double seconds)
  {
    const auto steps = static_cast<int>(seconds / tick_period_s_ + 0.5);
    for (int i = 0; i < steps; ++i) {step();}
  }

  // Posición del primer dron con ese id.
  Vec3 position_of(DroneId id) const
  {
    for (const auto & drone : drones_) {
      if (drone->state().drone_id == id) {return drone->state().position;}
    }
    throw std::out_of_range("no hay ningún dron con ese id en el bus");
  }

private:
  double tick_period_s_;
  double now_s_ = 0.0;
  int next_instance_ = 1;
  std::optional<Landmarks> hand_;
  std::vector<std::unique_ptr<DroneController>> drones_;
};

}  // namespace swarm_drone::testing
