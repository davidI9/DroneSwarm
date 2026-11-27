"""Resolución de la configuración de Zenoh a partir de la `.env`.

Se invoca el script en su modo de solo ver (`show`) como un proceso, con
`.env` temporales y sin ROS, y se comprueba solo lo que imprime y cómo termina.
"""

import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'zenoh_config.py'


def show(tmp_path, env_text):
    env_file = tmp_path / '.env'
    if env_text is not None:
        env_file.write_text(env_text)
    return subprocess.run(
        [sys.executable, str(SCRIPT), '--env', str(env_file), 'show'],
        capture_output=True, text=True,
    )


def section(output, name):
    """Línea `<name>: ...` de la salida de `show`."""
    for line in output.splitlines():
        if line.startswith(f'{name}:'):
            return line
    raise AssertionError(f'no hay línea "{name}:" en:\n{output}')


def test_cliente_router_conecta_a_la_torre_por_tailscale(tmp_path):
    result = show(tmp_path, 'ROL=cliente\nTORRE_TAILSCALE_IP=100.64.0.7\nTORRE_IP_VMS=192.168.122.1\n')

    assert result.returncode == 0, result.stderr
    assert 'connect/endpoints=["tcp/100.64.0.7:7447"]' in section(result.stdout, 'router')


def test_torre_router_escucha_en_todas_las_interfaces_y_no_conecta(tmp_path):
    result = show(tmp_path, 'ROL=torre\nTORRE_TAILSCALE_IP=100.64.0.7\nTORRE_IP_VMS=192.168.122.1\n')

    assert result.returncode == 0, result.stderr
    router = section(result.stdout, 'router')
    assert 'listen/endpoints=["tcp/0.0.0.0:7447"]' in router
    assert 'connect/endpoints=[]' in router


def test_vm_no_tiene_router_y_la_sesion_va_en_modo_client_hacia_el_host(tmp_path):
    result = show(tmp_path, 'ROL=vm\nTORRE_TAILSCALE_IP=100.64.0.7\nTORRE_IP_VMS=192.168.122.1\n')

    assert result.returncode == 0, result.stderr
    assert 'ZENOH_CONFIG_OVERRIDE' not in section(result.stdout, 'router')
    session = section(result.stdout, 'session')
    assert 'mode="client"' in session
    assert 'connect/endpoints=["tcp/192.168.122.1:7447"]' in session
    # Si el router del host aún no está, la sesión espera en vez de abortar.
    assert 'connect/timeout_ms=-1' in session


def test_sin_env_termina_con_error_que_nombra_el_fichero(tmp_path):
    result = show(tmp_path, None)

    assert result.returncode != 0
    assert '.env' in result.stderr
    assert 'no existe' in result.stderr


def test_sin_rol_termina_con_error_que_nombra_la_clave(tmp_path):
    result = show(tmp_path, 'TORRE_TAILSCALE_IP=100.64.0.7\nTORRE_IP_VMS=192.168.122.1\n')

    assert result.returncode != 0
    assert 'falta la clave ROL' in result.stderr


def test_cliente_sin_ip_de_tailscale_termina_con_error_que_nombra_la_clave(tmp_path):
    result = show(tmp_path, 'ROL=cliente\nTORRE_TAILSCALE_IP=\nTORRE_IP_VMS=192.168.122.1\n')

    assert result.returncode != 0
    assert 'falta la clave TORRE_TAILSCALE_IP' in result.stderr


def test_vm_sin_ip_del_host_termina_con_error_que_nombra_la_clave(tmp_path):
    result = show(tmp_path, 'ROL=vm\nTORRE_TAILSCALE_IP=100.64.0.7\n')

    assert result.returncode != 0
    assert 'falta la clave TORRE_IP_VMS' in result.stderr


def test_rol_invalido_termina_con_error_que_nombra_el_rol(tmp_path):
    result = show(tmp_path, 'ROL=servidor\nTORRE_TAILSCALE_IP=100.64.0.7\nTORRE_IP_VMS=192.168.122.1\n')

    assert result.returncode != 0
    assert 'ROL "servidor" no es válido' in result.stderr


def test_ip_mal_formada_termina_con_error_que_nombra_la_clave_y_el_valor(tmp_path):
    result = show(tmp_path, 'ROL=cliente\nTORRE_TAILSCALE_IP=100.64.0.300\nTORRE_IP_VMS=192.168.122.1\n')

    assert result.returncode != 0
    assert 'TORRE_TAILSCALE_IP' in result.stderr
    assert '"100.64.0.300" no es una IP válida' in result.stderr


def run_exec(tmp_path, env_text, target, command):
    env_file = tmp_path / '.env'
    env_file.write_text(env_text)
    return subprocess.run(
        [sys.executable, str(SCRIPT), '--env', str(env_file), 'exec', target, '--', *command],
        capture_output=True, text=True,
    )


PRINT_OVERRIDE = [sys.executable, '-c', 'import os; print(os.environ["ZENOH_CONFIG_OVERRIDE"])']


def test_exec_session_lanza_el_comando_con_la_configuracion_de_sesion(tmp_path):
    result = run_exec(tmp_path, 'ROL=vm\nTORRE_IP_VMS=192.168.122.1\n', 'session', PRINT_OVERRIDE)

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip().startswith('mode="client";connect/endpoints=["tcp/192.168.122.1:7447"]')


def test_exec_router_en_una_vm_se_niega_a_arrancar(tmp_path):
    result = run_exec(tmp_path, 'ROL=vm\nTORRE_IP_VMS=192.168.122.1\n', 'router', PRINT_OVERRIDE)

    assert result.returncode != 0
    assert result.stdout == ''
    assert 'en una VM no hay router' in result.stderr


def test_admite_export_comillas_y_comentarios_al_final_de_linea(tmp_path):
    result = show(tmp_path, 'export ROL="cliente"\nTORRE_TAILSCALE_IP=\'100.64.0.7\'  # tailscale ip -4\n')

    assert result.returncode == 0, result.stderr
    assert 'connect/endpoints=["tcp/100.64.0.7:7447"]' in section(result.stdout, 'router')
