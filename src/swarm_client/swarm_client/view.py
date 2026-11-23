"""Vista del enjambre: lo que el cliente muestra del estado que publican los drones.

No depende de rclpy: la hora se pasa en cada llamada, en segundos del reloj local
del cliente. Nunca se usa la marca de tiempo del mensaje, porque los relojes entre
máquinas no están sincronizados (invariante 7).
"""

from visualization_msgs.msg import MarkerArray

from swarm_client.conversion import Point
from swarm_client.publishing import swarm_markers


class SwarmView:
    """Último estado de cada dron vivo, con el mismo criterio de dron caído que el enjambre."""

    def __init__(self, drone_timeout: float):
        self._drone_timeout = drone_timeout
        # drone_id -> (última posición, hora local de recepción)
        self._drones: dict[int, tuple[Point, float]] = {}

    def on_state(self, drone_id: int, position: Point, now: float) -> None:
        """Registra el último estado de un dron con la hora local de recepción."""
        self._drones[drone_id] = (position, now)

    def markers(self, now: float) -> MarkerArray:
        """Olvida a los drones caídos y devuelve el `MarkerArray` del resto, sin marca de tiempo."""
        # Invariante 6: caído si lleva estrictamente más de drone_timeout sin estado.
        self._drones = {
            drone_id: (position, received)
            for drone_id, (position, received) in self._drones.items()
            if now - received <= self._drone_timeout
        }
        return swarm_markers(
            sorted((drone_id, position) for drone_id, (position, _) in self._drones.items())
        )
