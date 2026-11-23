"""Nodo de la vista del enjambre: capa fina alrededor de `SwarmView`.

Escucha `/swarm/state` y publica a ritmo fijo en `/swarm/markers` un único
`MarkerArray` con todo el enjambre. Es el único publicador de `/swarm/markers`.
"""

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from swarm_interfaces.msg import DroneState
from visualization_msgs.msg import MarkerArray

from swarm_client.view import SwarmView

# La misma QoS con que los drones se escuchan entre sí: un paquete perdido es
# solo una posición menos, nunca un bloqueo.
SWARM_STATE_QOS = QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT)


def _seconds(time) -> float:
    return time.nanoseconds * 1e-9


class SwarmViewNode(Node):
    def __init__(self):
        super().__init__('swarm_view')
        drone_timeout = self.declare_parameter('drone_timeout', 0.5).value
        rate_hz = self.declare_parameter('rate_hz', 30.0).value

        self._view = SwarmView(drone_timeout=drone_timeout)
        # Tráfico local al cliente: la QoS por defecto (RELIABLE) no es problema.
        self._markers_pub = self.create_publisher(MarkerArray, '/swarm/markers', 10)
        self.create_subscription(DroneState, '/swarm/state', self._on_state, SWARM_STATE_QOS)
        self.create_timer(1.0 / rate_hz, self._on_timer)
        self.get_logger().info(
            f'Vista del enjambre a {rate_hz} Hz (dron caído tras {drone_timeout} s sin estado)'
        )

    def _on_state(self, msg: DroneState) -> None:
        p = msg.position
        # Hora local de recepción, nunca msg.header.stamp (invariante 7).
        self._view.on_state(msg.drone_id, (p.x, p.y, p.z), _seconds(self.get_clock().now()))

    def _on_timer(self) -> None:
        now = self.get_clock().now()
        swarm = self._view.markers(_seconds(now))
        stamp = now.to_msg()
        for marker in swarm.markers:
            marker.header.stamp = stamp
        self._markers_pub.publish(swarm)


def main():
    rclpy.init()
    try:
        rclpy.spin(SwarmViewNode())
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        rclpy.try_shutdown()
