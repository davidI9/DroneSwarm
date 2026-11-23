"""Publicación del cliente: la mano (visión y mano falsa) y la vista del enjambre.

El módulo no importa rclpy al cargarse, para que la vista del enjambre se pueda
usar y probar sin él; solo `HandPublisher`, que necesita un nodo, lo carga.
"""

from collections.abc import Sequence
from typing import TYPE_CHECKING

from geometry_msgs.msg import Point
from swarm_interfaces.msg import HandLandmarks
from visualization_msgs.msg import Marker, MarkerArray

from swarm_client.conversion import Point as WorldPoint
from swarm_client.hand import HAND_CONNECTIONS

FRAME_ID = 'map'

if TYPE_CHECKING:
    from rclpy.node import Node


class HandPublisher:
    """Publica `/hand/landmarks` y la mano objetivo en `/hand/markers`."""

    def __init__(self, node: 'Node'):
        from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy

        # Fiable y persistente: un dron que arranca tarde recibe la última mano.
        landmarks_qos = QoSProfile(
            depth=1,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
        )
        self._node = node
        self._landmarks_pub = node.create_publisher(HandLandmarks, '/hand/landmarks', landmarks_qos)
        self._markers_pub = node.create_publisher(MarkerArray, '/hand/markers', 10)

    def publish(self, world_points: Sequence[WorldPoint]) -> None:
        stamp = self._node.get_clock().now().to_msg()
        points = [Point(x=x, y=y, z=z) for x, y, z in world_points]

        msg = HandLandmarks()
        msg.header.stamp = stamp
        msg.header.frame_id = FRAME_ID
        msg.landmarks = points
        self._landmarks_pub.publish(msg)

        self._markers_pub.publish(_hand_markers(points, stamp))


def _hand_markers(points: list[Point], stamp) -> MarkerArray:
    landmarks_marker = Marker()
    landmarks_marker.header.frame_id = FRAME_ID
    landmarks_marker.header.stamp = stamp
    landmarks_marker.ns = 'hand_landmarks'
    landmarks_marker.id = 0
    landmarks_marker.type = Marker.SPHERE_LIST
    landmarks_marker.action = Marker.ADD
    landmarks_marker.pose.orientation.w = 1.0
    landmarks_marker.scale.x = landmarks_marker.scale.y = landmarks_marker.scale.z = 0.08
    landmarks_marker.color.r, landmarks_marker.color.g, landmarks_marker.color.b, landmarks_marker.color.a = 1.0, 0.6, 0.1, 1.0
    landmarks_marker.points = points

    bones = Marker()
    bones.header = landmarks_marker.header
    bones.ns = 'hand_bones'
    bones.id = 0
    bones.type = Marker.LINE_LIST
    bones.action = Marker.ADD
    bones.pose.orientation.w = 1.0
    bones.scale.x = 0.03
    bones.color.r, bones.color.g, bones.color.b, bones.color.a = 1.0, 0.8, 0.4, 0.8
    bones.points = [points[i] for bone in HAND_CONNECTIONS for i in bone]

    return MarkerArray(markers=[landmarks_marker, bones])


def swarm_markers(drones: Sequence[tuple[int, WorldPoint]]) -> MarkerArray:
    """Markers de la vista del enjambre para `/swarm/markers`, sin marca de tiempo.

    Un `DELETEALL` que borra lo anterior (incluidas las etiquetas de los drones
    caídos), una `SPHERE_LIST` con todos los drones y una etiqueta por dron, en el
    orden de `drones`. Ninguno lleva tiempo de vida: quitar a los caídos es trabajo
    de la vista, no de RViz2.
    """
    delete_all = Marker()
    delete_all.header.frame_id = FRAME_ID
    delete_all.action = Marker.DELETEALL
    if not drones:
        return MarkerArray(markers=[delete_all])

    spheres = Marker()
    spheres.header.frame_id = FRAME_ID
    spheres.ns = 'drones'
    spheres.id = 0
    spheres.type = Marker.SPHERE_LIST
    spheres.action = Marker.ADD
    spheres.pose.orientation.w = 1.0
    spheres.scale.x = spheres.scale.y = spheres.scale.z = 0.3
    spheres.color.r, spheres.color.g, spheres.color.b, spheres.color.a = 0.1, 0.8, 0.2, 1.0
    spheres.points = [Point(x=x, y=y, z=z) for _, (x, y, z) in drones]

    labels = []
    for drone_id, (x, y, z) in drones:
        label = Marker()
        label.header.frame_id = FRAME_ID
        label.ns = 'drone_ids'
        label.id = drone_id
        label.type = Marker.TEXT_VIEW_FACING
        label.action = Marker.ADD
        label.text = str(drone_id)
        label.pose.position = Point(x=x, y=y, z=z + 0.35)
        label.pose.orientation.w = 1.0
        label.scale.z = 0.25
        label.color.r, label.color.g, label.color.b, label.color.a = 1.0, 1.0, 1.0, 1.0
        labels.append(label)

    return MarkerArray(markers=[delete_all, spheres, *labels])
