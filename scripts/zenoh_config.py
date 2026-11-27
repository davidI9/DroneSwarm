#!/usr/bin/env python3
"""Configuración de Zenoh a partir de la `.env` según el ROL de la máquina.

Lee la `.env` (por defecto la de la raíz del repositorio) y construye
`ZENOH_CONFIG_OVERRIDE` para el router o para las sesiones.

    zenoh_config.py show                    # imprime la configuración resuelta
    zenoh_config.py exec router  -- CMD...  # lanza CMD con la del router
    zenoh_config.py exec session -- CMD...  # lanza CMD con la de las sesiones
"""

import argparse
import ipaddress
import os
import sys
from pathlib import Path

DEFAULT_ENV = Path(__file__).resolve().parents[1] / '.env'
PORT = 7447
ROLES = ('cliente', 'torre', 'vm')


class ConfigError(Exception):
    """Fallo en la `.env`; el mensaje nombra el problema."""


def parse_env(path):
    if not path.is_file():
        raise ConfigError(
            f'{path} no existe. Cópiala de .env.example y rellénala para esta máquina.')
    values = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        key, _, value = line.partition('=')
        key = key.strip().removeprefix('export ').strip()
        value = value.split(' #', 1)[0].strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
            value = value[1:-1]
        values[key] = value
    return values


def require(values, key):
    value = values.get(key, '')
    if not value:
        raise ConfigError(f'falta la clave {key} en la .env (ver .env.example).')
    return value


def require_endpoint(values, key):
    """Endpoint TCP del router en la IP de la clave `key`."""
    value = require(values, key)
    try:
        ip = ipaddress.ip_address(value)
    except ValueError:
        raise ConfigError(f'{key}: "{value}" no es una IP válida.') from None
    host = f'[{ip}]' if ip.version == 6 else str(ip)
    return f'tcp/{host}:{PORT}'


def override_string(pairs):
    return ';'.join(f'{key}={value}' for key, value in pairs)


def resolve(values):
    """Devuelve (router, session): listas de pares clave/valor json5.

    `router` es None en una VM, que no tiene router.
    """
    rol = require(values, 'ROL')
    if rol not in ROLES:
        raise ConfigError(f'el ROL "{rol}" no es válido: debe ser {", ".join(ROLES)}.')
    if rol == 'vm':
        host = require_endpoint(values, 'TORRE_IP_VMS')
        # En modo client, rmw_zenoh aborta si no alcanza el router al abrir la
        # sesión; con timeout -1 el dron espera a que arranque el del host.
        return None, [('mode', '"client"'), ('connect/endpoints', f'["{host}"]'),
                      ('connect/timeout_ms', '-1')]

    # El router escucha en IPv4 y las sesiones locales conectan a 127.0.0.1, no
    # a `localhost`, que puede resolverse a ::1.
    listen = ('listen/endpoints', f'["tcp/0.0.0.0:{PORT}"]')
    if rol == 'cliente':
        torre = require_endpoint(values, 'TORRE_TAILSCALE_IP')
        router = [listen, ('connect/endpoints', f'["{torre}"]')]
    else:
        router = [listen, ('connect/endpoints', '[]')]
    session = [('mode', '"peer"'), ('connect/endpoints', f'["tcp/127.0.0.1:{PORT}"]')]
    return router, session


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--env', type=Path, default=DEFAULT_ENV)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('show', help='imprime la configuración resuelta sin lanzar nada')
    exec_parser = sub.add_parser('exec', help='lanza un comando con la configuración aplicada')
    exec_parser.add_argument('target', choices=('router', 'session'))
    exec_parser.add_argument('cmd', nargs=argparse.REMAINDER, help='-- comando y argumentos')
    args = parser.parse_args(argv)

    try:
        router, session = resolve(parse_env(args.env))
    except ConfigError as error:
        print(f'zenoh_config: {error}', file=sys.stderr)
        return 1

    if args.command == 'exec':
        cmd = args.cmd[1:] if args.cmd[:1] == ['--'] else args.cmd
        if not cmd:
            parser.error('exec necesita un comando tras --')
        pairs = router if args.target == 'router' else session
        if pairs is None:
            print('zenoh_config: en una VM no hay router; los drones de la VM conectan '
                  'en modo client al router del host (TORRE_IP_VMS).', file=sys.stderr)
            return 1
        os.environ['ZENOH_CONFIG_OVERRIDE'] = override_string(pairs)
        os.execvp(cmd[0], cmd)

    if router is None:
        print('router: ninguno (en una VM no hay router)')
    else:
        print(f'router: ZENOH_CONFIG_OVERRIDE={override_string(router)}')
    print(f'session: ZENOH_CONFIG_OVERRIDE={override_string(session)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
