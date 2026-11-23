import pytest
from visualization_msgs.msg import Marker

from swarm_client.view import SwarmView

TIMEOUT = 0.5


def spheres(array):
    [sphere] = [m for m in array.markers if m.ns == 'drones']
    return sphere


def labels(array):
    return [m for m in array.markers if m.ns == 'drone_ids']


def positions(array):
    return [(p.x, p.y, p.z) for p in spheres(array).points]


def label_ids(array):
    return [m.id for m in labels(array)]


def two_drones():
    view = SwarmView(drone_timeout=TIMEOUT)
    view.on_state(1, (1.0, 2.0, 3.0), 10.0)
    view.on_state(2, (-1.0, 0.0, 2.0), 10.0)
    return view.markers(10.1)


def test_sin_ningun_estado_el_array_solo_contiene_el_deleteall():
    markers = SwarmView(drone_timeout=TIMEOUT).markers(10.0).markers
    assert len(markers) == 1
    assert markers[0].action == Marker.DELETEALL


def test_un_dron_que_envia_estado_aparece_en_su_posicion_con_su_etiqueta():
    view = SwarmView(drone_timeout=TIMEOUT)
    view.on_state(7, (1.0, -2.0, 3.0), 10.0)

    array = view.markers(10.1)

    assert spheres(array).type == Marker.SPHERE_LIST
    assert positions(array) == [(1.0, -2.0, 3.0)]
    [label] = labels(array)
    assert label.type == Marker.TEXT_VIEW_FACING
    assert label.id == 7
    assert label.text == '7'


def test_con_varios_drones_hay_un_punto_y_una_etiqueta_por_dron_ordenados_por_id():
    view = SwarmView(drone_timeout=TIMEOUT)
    view.on_state(5, (5.0, 0.0, 1.0), 10.0)
    view.on_state(0, (0.0, 0.0, 1.0), 10.0)
    view.on_state(12, (12.0, 0.0, 1.0), 10.0)

    array = view.markers(10.1)

    assert positions(array) == [(0.0, 0.0, 1.0), (5.0, 0.0, 1.0), (12.0, 0.0, 1.0)]
    assert label_ids(array) == [0, 5, 12]
    assert [m.text for m in labels(array)] == ['0', '5', '12']


def test_si_un_dron_envia_varios_estados_se_muestra_la_ultima_posicion():
    view = SwarmView(drone_timeout=TIMEOUT)
    view.on_state(3, (0.0, 0.0, 1.0), 10.0)
    view.on_state(3, (0.5, 0.2, 1.1), 10.05)

    assert positions(view.markers(10.1)) == [(0.5, 0.2, 1.1)]


def test_un_dron_sin_estado_durante_mas_del_timeout_desaparece_de_esferas_y_etiquetas():
    view = SwarmView(drone_timeout=TIMEOUT)
    view.on_state(4, (0.0, 0.0, 1.0), 10.0)

    markers = view.markers(10.0 + TIMEOUT + 0.01).markers

    assert len(markers) == 1
    assert markers[0].action == Marker.DELETEALL


def test_justo_en_el_limite_del_timeout_el_dron_sigue_apareciendo():
    view = SwarmView(drone_timeout=0.25)
    view.on_state(4, (0.0, 0.0, 1.0), 10.0)

    array = view.markers(10.25)

    assert positions(array) == [(0.0, 0.0, 1.0)]
    assert label_ids(array) == [4]


def test_un_dron_caido_que_vuelve_a_enviar_estado_reaparece():
    view = SwarmView(drone_timeout=TIMEOUT)
    view.on_state(4, (0.0, 0.0, 1.0), 10.0)
    view.markers(11.0)
    view.on_state(4, (1.0, 1.0, 1.0), 12.0)

    array = view.markers(12.1)

    assert positions(array) == [(1.0, 1.0, 1.0)]
    assert label_ids(array) == [4]


def test_si_cae_un_dron_y_los_demas_siguen_solo_desaparece_el():
    view = SwarmView(drone_timeout=TIMEOUT)
    for drone_id in (1, 2, 3):
        view.on_state(drone_id, (float(drone_id), 0.0, 1.0), 10.0)
    for drone_id in (1, 3):
        view.on_state(drone_id, (float(drone_id), 0.0, 1.0), 10.4)

    array = view.markers(10.6)

    assert positions(array) == [(1.0, 0.0, 1.0), (3.0, 0.0, 1.0)]
    assert label_ids(array) == [1, 3]


def test_el_primer_marker_es_deleteall_y_ninguno_tiene_tiempo_de_vida():
    markers = two_drones().markers

    assert markers[0].action == Marker.DELETEALL
    assert all(m.action == Marker.ADD for m in markers[1:])
    assert all(m.lifetime.sec == 0 and m.lifetime.nanosec == 0 for m in markers)


def test_todos_los_markers_van_en_el_frame_map():
    assert all(m.header.frame_id == 'map' for m in two_drones().markers)


def test_las_esferas_conservan_el_aspecto_acordado():
    sphere = spheres(two_drones())

    assert (sphere.scale.x, sphere.scale.y, sphere.scale.z) == pytest.approx((0.3, 0.3, 0.3))
    color = sphere.color
    assert (color.r, color.g, color.b, color.a) == pytest.approx((0.1, 0.8, 0.2, 1.0))
    assert sphere.pose.orientation.w == 1.0


def test_las_etiquetas_van_por_encima_de_la_esfera_en_blanco():
    label = labels(two_drones())[0]

    position = label.pose.position
    assert (position.x, position.y, position.z) == pytest.approx((1.0, 2.0, 3.35))
    assert label.scale.z == pytest.approx(0.25)
    color = label.color
    assert (color.r, color.g, color.b, color.a) == pytest.approx((1.0, 1.0, 1.0, 1.0))
    assert label.pose.orientation.w == 1.0


def test_la_vista_del_enjambre_se_usa_sin_rclpy():
    import subprocess
    import sys

    code = 'import sys, swarm_client.view; assert "rclpy" not in sys.modules'
    subprocess.run([sys.executable, '-c', code], check=True)
