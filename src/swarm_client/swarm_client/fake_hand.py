"""Mano falsa: publica una mano sin necesidad de cámara.

Por ahora solo tiene modo estático: una mano abierta fija.
"""

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node

from swarm_client.conversion import landmarks_to_world
from swarm_client.hand import OPEN_HAND
from swarm_client.publishing import HandPublisher


class FakeHand(Node):
    def __init__(self):
        super().__init__('fake_hand')
        scale = self.declare_parameter('scale', 4.0).value
        height = self.declare_parameter('height', 2.5).value
        rate_hz = self.declare_parameter('rate_hz', 10.0).value

        self._hand = landmarks_to_world(OPEN_HAND, scale=scale, height=height)
        self._publisher = HandPublisher(self)
        self.create_timer(1.0 / rate_hz, self._on_timer)
        self.get_logger().info(f'Mano falsa estática (escala {scale} m, altura {height} m)')

    def _on_timer(self):
        self._publisher.publish(self._hand)


def main():
    rclpy.init()
    try:
        rclpy.spin(FakeHand())
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        rclpy.try_shutdown()
