"""Mano falsa: publica una mano sin necesidad de cámara.

Tiene dos modos, elegidos con el parámetro `mode`:

- `static`: una mano abierta fija, para observar la convergencia a un equilibrio;
- `animated`: la mano se abre y se cierra en bucle cada `period` segundos.
"""

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node

from swarm_client.conversion import landmarks_to_world
from swarm_client.hand import fake_hand_pose
from swarm_client.publishing import HandPublisher


class FakeHand(Node):
    def __init__(self):
        super().__init__('fake_hand')
        self._mode = self.declare_parameter('mode', 'static').value
        self._period = self.declare_parameter('period', 4.0).value
        self._scale = self.declare_parameter('scale', 4.0).value
        self._height = self.declare_parameter('height', 2.5).value
        rate_hz = self.declare_parameter('rate_hz', 10.0).value

        # Falla al arrancar, no en el primer tick, si el modo o el periodo no valen.
        fake_hand_pose(self._mode, 0.0, period=self._period)

        self._publisher = HandPublisher(self)
        self._start = self.get_clock().now()
        self.create_timer(1.0 / rate_hz, self._on_timer)
        detail = f', periodo {self._period} s' if self._mode == 'animated' else ''
        self.get_logger().info(
            f'Mano falsa en modo {self._mode} (escala {self._scale} m, altura {self._height} m{detail})'
        )

    def _on_timer(self):
        t = (self.get_clock().now() - self._start).nanoseconds * 1e-9
        pose = fake_hand_pose(self._mode, t, period=self._period)
        self._publisher.publish(landmarks_to_world(pose, scale=self._scale, height=self._height))


def main():
    rclpy.init()
    try:
        rclpy.spin(FakeHand())
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        rclpy.try_shutdown()
