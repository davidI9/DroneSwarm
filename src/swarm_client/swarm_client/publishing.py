"""Publicación de la mano, compartida por la visión y la mano falsa."""

from collections.abc import Sequence

from geometry_msgs.msg import Point
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from swarm_interfaces.msg import HandLandmarks
from visualization_msgs.msg import Marker, MarkerArray

from swarm_client.conversion import Point as WorldPoint
from swarm_client.hand import HAND_CONNECTIONS

FRAME_ID = 'map'

# Fiable y persistente: un dron que arranca tarde recibe la última mano.
LANDMARKS_QOS = QoSProfile(
    depth=1,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
)


class HandPublisher:
    """Publica `/hand/landmarks` y la mano objetivo en `/hand/markers`."""

    def __init__(self, node: Node):
        self._node = node
        self._landmarks_pub = node.create_publisher(HandLandmarks, '/hand/landmarks', LANDMARKS_QOS)
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
