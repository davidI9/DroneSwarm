import pytest

from swarm_client.hand import CLOSED_HAND, OPEN_HAND, fake_hand_pose

PERIOD = 4.0


def assert_pose(actual, expected):
    assert len(actual) == 21
    for p, q in zip(actual, expected):
        assert p == pytest.approx(q)


@pytest.mark.parametrize('t', [0.0, 1.3, 2.0, 57.9])
def test_en_modo_estatico_la_mano_queda_abierta_en_todo_momento(t):
    assert_pose(fake_hand_pose('static', t, period=PERIOD), OPEN_HAND)


def test_en_modo_animado_la_mano_empieza_abierta_y_a_medio_periodo_es_un_puno():
    assert_pose(fake_hand_pose('animated', 0.0, period=PERIOD), OPEN_HAND)
    assert_pose(fake_hand_pose('animated', PERIOD / 2, period=PERIOD), CLOSED_HAND)


def test_en_modo_animado_el_movimiento_se_repite_cada_periodo():
    for t in (0.3, 1.0, 2.7):
        assert_pose(
            fake_hand_pose('animated', t + 3 * PERIOD, period=PERIOD),
            fake_hand_pose('animated', t, period=PERIOD),
        )


def test_en_modo_animado_la_mano_cambia_de_forma_continua():
    dt = 0.01
    previous = fake_hand_pose('animated', 0.0, period=PERIOD)
    for step in range(1, int(PERIOD / dt) + 1):
        current = fake_hand_pose('animated', step * dt, period=PERIOD)
        assert max(abs(a - b) for p, q in zip(previous, current) for a, b in zip(p, q)) < 0.01
        previous = current


def test_a_un_cuarto_de_periodo_la_mano_esta_a_medio_cerrar():
    middle = fake_hand_pose('animated', PERIOD / 4, period=PERIOD)
    halfway = [
        tuple((a + b) / 2 for a, b in zip(p, q)) for p, q in zip(OPEN_HAND, CLOSED_HAND)
    ]
    assert_pose(middle, halfway)


def test_un_modo_desconocido_es_un_error():
    with pytest.raises(ValueError, match='modo'):
        fake_hand_pose('wave', 0.0, period=PERIOD)


@pytest.mark.parametrize('period', [0.0, -1.0])
def test_en_modo_animado_un_periodo_no_positivo_es_un_error(period):
    with pytest.raises(ValueError, match='periodo'):
        fake_hand_pose('animated', 0.0, period=period)
