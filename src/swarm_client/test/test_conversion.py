import pytest

from swarm_client.conversion import landmarks_to_world


def convert(point, scale=4.0, height=2.5):
    return landmarks_to_world([point], scale=scale, height=height)[0]


def test_el_centro_de_la_imagen_queda_en_el_eje_a_la_altura_configurada():
    assert convert((0.5, 0.5, 0.0), height=2.5) == pytest.approx((0.0, 0.0, 2.5))


def test_la_x_de_la_imagen_va_al_eje_y_del_mundo():
    x, y, z = convert((1.0, 0.5, 0.0), scale=4.0, height=2.5)
    assert (x, y, z) == pytest.approx((0.0, 2.0, 2.5))


def test_bajar_en_la_imagen_es_bajar_en_el_mundo():
    _, _, z_arriba = convert((0.5, 0.0, 0.0), scale=4.0, height=2.5)
    _, _, z_abajo = convert((0.5, 1.0, 0.0), scale=4.0, height=2.5)
    assert z_arriba == pytest.approx(4.5)
    assert z_abajo == pytest.approx(0.5)


def test_la_profundidad_va_al_eje_x_del_mundo():
    assert convert((0.5, 0.5, 0.25), scale=4.0) == pytest.approx((1.0, 0.0, 2.5))


def test_la_escala_multiplica_las_distancias_entre_landmarks():
    a, b = landmarks_to_world([(0.4, 0.4, 0.0), (0.6, 0.4, 0.0)], scale=4.0, height=2.5)
    c, d = landmarks_to_world([(0.4, 0.4, 0.0), (0.6, 0.4, 0.0)], scale=8.0, height=2.5)
    assert b[1] - a[1] == pytest.approx(0.8)
    assert d[1] - c[1] == pytest.approx(1.6)


def test_la_altura_desplaza_la_mano_en_vertical_sin_deformarla():
    bajo = landmarks_to_world([(0.3, 0.2, 0.1), (0.7, 0.9, -0.1)], scale=4.0, height=1.0)
    alto = landmarks_to_world([(0.3, 0.2, 0.1), (0.7, 0.9, -0.1)], scale=4.0, height=3.0)
    for p, q in zip(bajo, alto):
        assert (q[0], q[1], q[2] - 2.0) == pytest.approx(p)
