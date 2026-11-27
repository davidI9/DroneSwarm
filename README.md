# DroneSwarm

Enjambre de drones simulados sobre ROS 2 que, sin coordinador central, replica en tiempo real la forma de una mano capturada por la cámara del cliente. Cada dron se mueve por descenso de gradiente sobre una teselación de Voronoi centroidal (CVT).

Esta es la rama `zenoh-version`: la red usa `rmw_zenoh_cpp`, con un router en el cliente y otro en el servidor, unidos por Tailscale. La rama `main` resuelve el mismo problema con DDS sobre VXLAN. La teoría de la red está en `docs/teoria.md`.

## Requisitos

- [pixi](https://pixi.sh) en cada máquina que ejecute algún proceso: el cliente, el host del servidor y cada VM. Pixi instala ROS 2, `rmw_zenoh_cpp` y las demás dependencias.
- Tailscale en el cliente y en el host del servidor, los dos en el mismo tailnet. Las VMs no lo necesitan.
- Una red interna entre el host y sus VMs (por ejemplo, la red `default` de libvirt, con el host en `192.168.122.1`).

En cada máquina, una vez clonado el repositorio:

```bash
pixi run build
```

Todas las tareas de pixi usan `rmw_zenoh_cpp`, así que no hay que exportar `RMW_IMPLEMENTATION`.

## Configuración de cada máquina: la `.env`

Cada máquina tiene su propia `.env` en la raíz del repositorio, que no se versiona. Se parte de la plantilla, que explica cada clave:

```bash
cp .env.example .env
```

| Clave | Para qué sirve | Hace falta con |
|---|---|---|
| `ROL` | Papel de la máquina: `cliente`, `torre` o `vm` | todas |
| `TORRE_TAILSCALE_IP` | IP de Tailscale de la torre (`tailscale ip -4` en la torre), a la que conecta el router del cliente | `ROL=cliente` |
| `TORRE_IP_VMS` | IP del host vista desde las VMs (en libvirt, `ip -4 addr show virbr0` en el host) | `ROL=vm` |

Para ver la configuración de Zenoh que resulta de la `.env`, sin lanzar nada:

```bash
pixi run zenoh-config
```

Si falta la `.env`, falta una clave, el `ROL` no es válido o una IP está mal formada, esta tarea y todas las que lanzan nodos terminan con un mensaje que nombra el problema.

## Levantar cada máquina

Cada proceso va en su propia terminal. El router se lanza primero.

### Torre (`ROL=torre`)

```bash
pixi run router
```

El router del host solo escucha, en TCP 7447 en todas las interfaces. No necesita la IP del portátil. El host no ejecuta drones; si se lanza algún nodo en él, su sesión va en modo peer contra este router.

### VM (`ROL=vm`)

En las VMs no hay router (`pixi run router` se niega a arrancar). Los drones abren su sesión en modo client contra el router del host, en `TORRE_IP_VMS`, y si ese router aún no está, esperan a que arranque.

```bash
pixi run drone 3                                          # un dron, en su terminal
pixi run drone 3 "-p spawn_x:=1.0 -p spawn_y:=0.0 -p spawn_z:=1.0"
scripts/launch_swarm.sh 10 100                            # drones 100..109 de golpe
```

Varios drones conviven en la misma VM, cada uno con su nodo `drone_<id>`. Los `drone_id` tienen que ser únicos en todo el enjambre: si dos drones comparten id, los dos terminan con un error fatal en cuanto reciben el estado del otro.

### Cliente (`ROL=cliente`)

```bash
pixi run router                 # conecta al router de la torre por Tailscale
pixi run fake-hand animated     # o `static`
pixi run swarm-view
pixi run rviz                   # en Arch y similares: pixi run rviz-arch
```

Las sesiones del cliente van en modo peer contra su router local. Si la torre aún no está, el router del cliente sigue intentando conectar.

## Probar todo en el portátil, sin torre

Con `ROL=cliente` se puede lanzar el enjambre entero en el portátil. El router del cliente no alcanza la torre, pero sigue funcionando para las sesiones locales, así que `TORRE_TAILSCALE_IP` puede quedarse con el valor de la plantilla.

```bash
pixi run router
pixi run fake-hand animated
scripts/launch_swarm.sh 5       # drones 0..4, en segundo plano, logs en log/drones/
pixi run swarm-view
pixi run rviz
```

Los drones convergen sobre la mano. Para comprobar la detección de id duplicado, lanza en otra terminal `pixi run drone 0`: los dos drones con id 0 terminan con error fatal.

## Firewall y Tailscale

En el host de la torre hay que abrir **TCP 7447**, que es el puerto del router, en dos interfaces:

- la de Tailscale (`tailscale0`), por la que llega el router del cliente;
- la de la red interna de las VMs (en libvirt, `virbr0`), por la que llegan los drones.

Con UFW:

```bash
sudo ufw allow in on tailscale0 to any port 7447 proto tcp
sudo ufw allow in on virbr0 to any port 7447 proto tcp
```

Si no hay cortafuegos activo (`sudo ufw status`, `sudo nft list ruleset`), no hace falta nada, pero conviene comprobarlo en lugar de suponerlo. Como el router escucha en todas las interfaces, no abras el puerto en las demás (por ejemplo, la de la LAN de casa).

El cliente no necesita abrir ningún puerto: nadie se conecta a él. Su router también escucha en el 7447, así que en una red ajena (la de la universidad, por ejemplo) conviene que el firewall del portátil no acepte conexiones entrantes a ese puerto.

Si las ACLs del tailnet están restringidas, tienen que permitir al portátil conectar a `torre:7447` por TCP.

## Qué pasa si algo cae

- **Una VM**: sus drones dejan de publicar estado y el resto del enjambre los trata como drones caídos pasado `neighbor_timeout` (0.5 s por defecto). Los demás absorben su parte del dominio.
- **El router del cliente**: el cliente deja de ver el enjambre, pero los drones siguen viéndose a través del router del host y mantienen la última mano.
- **El proceso del router del host**: todo el servidor queda aislado. Las VMs van en modo client, así que todo su tráfico, incluido el de dos drones de la misma VM, pasaba por ese router. Cada dron deja de ver a sus vecinos, los da por drones caídos y se dirige al centroide de toda la última mano que conoce; la capa de seguridad sigue activa, pero no puede separarlo de vecinos de los que no sabe nada. Es el único punto de fallo de la topología, y se acepta porque el host ya es un punto de fallo compartido por todas sus VMs (ver `docs/teoria.md`, §1).

## Tests

```bash
pixi run test          # gtest del dron, pytest del cliente y de la configuración de Zenoh
pixi run test-config   # solo la configuración de Zenoh, sin ROS
```
