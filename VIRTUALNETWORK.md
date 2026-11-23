# Plan: red de capa 2 (VXLAN sobre WireGuard) entre el cliente y las VMs

Este documento es el plan para montar a mano la red que une el portátil (cliente) con las VMs de la torre (servidor), de modo que el descubrimiento multicast de DDS funcione sin tocar la configuración de ROS 2. La teoría de por qué hace falta una red de capa 2 está en `docs/teoria.md` §1.

Escenario: **el portátil está conectado por cable Ethernet a otra red (por ejemplo, la universidad) y llega por internet a la torre, que está en casa detrás del router.**

Tiene dos fases:

- **Fase 1, montaje manual y no persistente.** Se levanta todo con comandos sueltos y se verifica capa por capa. Un reinicio borra el overlay (WireGuard incluido, si no lo habilitas en systemd), y eso es una ventaja: si algo sale mal, se reinicia y se vuelve a empezar.
- **Fase 2, script.** Cuando la fase 1 funcione, se convierten esos mismos comandos en un script idempotente (`scripts/vxlan.sh`) que levanta, tira y diagnostica la red. Opcionalmente, se arranca con systemd.

El camino principal usa **WireGuard + un nombre en Cloudflare** para unir el portátil y la torre. Al final hay una **alternativa con Tailscale** (§A), que sustituye esa parte y deja igual todo lo de VXLAN, salvo el MTU. Lee §A.0 antes de empezar para elegir una de las dos.

---

## 0. Topología y decisiones

```
   Portátil (cliente)                                          Torre (servidor, en casa)
 ┌──────────────────────────┐                              ┌──────────────────────────────────────┐
 │ RViz2, visión/mano falsa │                              │ host: 10.200.0.1 en br-swarm         │
 │                          │                              │                                      │
 │ vxlan0  10.200.0.2/24    │  capa 2 (overlay del enjambre) │ br-swarm ── vxlan0                 │
 │   │  (VXLAN, UDP 4789)   │                              │    ├── vnet0 ── VM1  10.200.0.11     │
 │ wg0     10.100.0.2/24 ═══╪════ túnel cifrado WireGuard ═══╪═ wg0  10.100.0.1/24                 │
 │   │  (UDP 51820)         │                              │    └── vnet1 ── VM2  10.200.0.12     │
 │ enp…  (cable, red uni)   │                              │ enp3s0 (LAN de casa)                 │
 └───┼──────────────────────┘                              └───┼──────────────────────────────────┘
     └──── internet ──── router de casa (NAT, reenvía UDP 51820 → torre) ────┘
```

Hay tres redes, una encima de otra:

1. **Underlay**: internet. Por aquí solo viaja UDP 51820 cifrado.
2. **WireGuard** (10.100.0.0/24): un enlace punto a punto, cifrado y con IPs fijas entre el portátil y la torre. Es de capa 3: transporta paquetes IP, no tramas Ethernet, así que no lleva multicast ni sirve por sí solo para DDS.
3. **Overlay VXLAN** (10.200.0.0/24): un switch Ethernet virtual que se transporta *dentro* de WireGuard. Para DDS, es una única LAN donde están el portátil, el host de la torre y todas las VMs, y por ella sí pasa el multicast de SPDP.

**¿Por qué hacen falta las dos capas?**

- **Sin WireGuard, VXLAN directa por internet no funciona.** La torre envía siempre a `IP_portátil:4789`, pero el NAT de la red del portátil no tiene ese puerto abierto, y el kernel cambia el puerto de origen de VXLAN según el flujo, así que el NAT no mantiene un mapeo estable. Además, VXLAN no va cifrada, y cualquiera que adivine el VNI podría inyectar tramas en tu red.
- WireGuard cifra el tráfico, atraviesa el NAT del portátil gracias a los *keepalives* y le da una IP fija (10.100.0.2) aunque cambie de red. Por eso VXLAN puede usar `local`/`remote` fijos.
- **Sin VXLAN, el descubrimiento de DDS no funciona.** WireGuard no transporta multicast, y la alternativa (un servidor de descubrimiento) se descartó por ser un punto único de fallo.

### Plan de direcciones

| Máquina | WireGuard (`wg0`) | Overlay | 
|---|---|---|
| Torre (host) | 10.100.0.1/24, escucha en UDP 51820 | `br-swarm` 10.200.0.1/24 |
| Portátil | 10.100.0.2/24 | `vxlan0` 10.200.0.2/24 |
| VM 1, VM 2, … | — | `ens3` 10.200.0.11, .12, … /24 |

VXLAN: VNI **42**, UDP **4789** (solo dentro de `wg0`).

Las VMs no necesitan WireGuard: llegan al portátil a través del bridge, que lleva sus tramas a `vxlan0`, y este las envía por `wg0`.

### MTU

El MTU es el tamaño máximo de paquete IP que cabe en una interfaz. Cada capa se come una parte:

| Capa | MTU | Por qué |
|---|---|---|
| Ethernet (underlay) | 1500 | Lo normal en una red cableada. |
| `wg0` | **1420** | WireGuard añade 80 bytes en el peor caso (IPv6 40 + UDP 8 + cabecera WireGuard 32). Es el valor por defecto de `wg-quick`. |
| Overlay (`br-swarm`, `vxlan0` y la NIC de cada VM) | **1370** | VXLAN añade 50 bytes (Ethernet interna 14 + VXLAN 8 + UDP 8 + IPv4 20): 1420 − 50. |

La regla es **MTU del overlay = MTU de `wg0` − 50**, y todas las interfaces del overlay deben tener el mismo valor.

Si en algún punto del camino el MTU real es menor (PPPoE en casa, que deja 1492; túneles en la red de la universidad), el ping pequeño funciona y los paquetes grandes se pierden en silencio. Es un error que cuesta mucho de diagnosticar, así que en §1.2 se mide en lugar de suponerse. Si falla, se baja `wg0` (por ejemplo, a 1380) y el overlay a 1330.

DDS seguirá fragmentando sus datagramas grandes en IP (Fast DDS envía hasta ~64 KB por datagrama). Eso es normal. El MTU correcto evita que sean los túneles los que fragmenten o pierdan paquetes.

---

## Fase 1: montaje manual, no persistente

Haz los pasos en orden y no avances hasta que pase la verificación de cada uno. Así, si algo falla, sabes en qué capa está el problema.

### 1.0 Comprobaciones previas

**¿Es la IP de casa alcanzable desde internet? (lo más importante)**

Entra en el panel del router de casa y mira la IP de la WAN. Compárala con tu IP pública, la que te muestra cualquier web de "cuál es mi IP" abierta desde casa.

- **Si coinciden**, tienes IP pública y el reenvío de puertos funcionará.
- **Si no coinciden, o la IP de la WAN es 100.64.x.x–100.127.x.x, 10.x, 172.16–31.x o 192.168.x**, estás detrás de **CG-NAT**: tu operador comparte una IP pública entre muchos clientes y ningún reenvío de puertos del router llegará a la torre. Hay dos salidas:
  1. Pedir al operador que te saque del CG-NAT (muchos lo hacen gratis por teléfono).
  2. Usar un VPS con IP pública como punto de encuentro de WireGuard. Este plan no lo cubre.

**¿Cambia la IP pública de casa?** En una línea doméstica, lo normal es que sí. Para no depender de ella, el portátil apunta a un nombre, `torre.tudominio.com`, de tu dominio gestionado en Cloudflare. El registro se mantiene al día con un cliente DDNS en la torre (§1.0.1).

### 1.0.1 Nombre de la torre en Cloudflare

**a) Registro DNS.** En el panel de Cloudflare, en *DNS → Records*, crea:

| Tipo | Nombre | Contenido | Proxy | TTL |
|---|---|---|---|---|
| A | `torre` | IP pública actual de casa | **DNS only (nube gris)** | 60 s o *Auto* |

- **Tipo A**: un nombre que resuelve a una dirección IPv4.
- **DNS only, nunca Proxied (nube naranja).** Con el proxy, el nombre resuelve a IPs de Cloudflare, que solo reenvían HTTP/HTTPS y descartan el UDP de WireGuard. El síntoma sería que no hay handshake nunca, y no hay ningún error que lo explique.
- **TTL bajo**: es el tiempo que los resolvers guardan la respuesta en caché. Con 60 s, un cambio de IP se nota en un minuto en lugar de en horas.
- El nombre publica la IP de tu casa en DNS público. Es normal en cualquier servidor doméstico, pero evita un nombre que diga qué hay detrás (`drones-sin-cifrar` no).

Comprobación desde cualquier máquina:

```bash
dig +short torre.tudominio.com    # debe dar tu IP pública, no una 104.x/172.6x de Cloudflare
```

**b) Token de API.** En Cloudflare, *My Profile → API Tokens → Create Token → Edit zone DNS*:

- Permiso: **Zone → DNS → Edit**.
- Zone Resources: **Include → Specific zone → tudominio.com**.

Así, el token solo puede editar los registros DNS de ese dominio. Va a estar guardado en la torre en texto plano, y si alguien lo roba no puede tocar nada más de tu cuenta. Cópialo al crearlo, porque Cloudflare no lo vuelve a mostrar.

**c) Cliente DDNS en la torre: `ddclient`.** Es un demonio que comprueba la IP pública cada cierto tiempo y, si ha cambiado, actualiza el registro con la API de Cloudflare.

```bash
sudo pacman -S ddclient        # o el gestor de paquetes de la distro de la torre
```

`/etc/ddclient.conf`:

```
daemon=300
ssl=yes
usev4=webv4, webv4=ipify-ipv4
protocol=cloudflare
zone=tudominio.com
login=token
password=<token de API>
torre.tudominio.com
```

- `daemon=300`: comprueba la IP cada 5 minutos.
- `usev4=webv4, webv4=ipify-ipv4`: obtiene la IP pública preguntando a un servicio web. La torre está detrás del NAT del router, así que la IP de su interfaz (192.168.x) no sirve.
- `zone`: el dominio raíz. La última línea es el registro que se actualiza.
- `login=token` + `password=<token>`: autenticación con el token de API de b), no con la clave global de la cuenta.
- La sintaxis de `usev4`/`webv4` es la de `ddclient` ≥ 3.11. Las versiones antiguas usan `use=web, web=…`. Comprueba la tuya con `ddclient --version` y `man ddclient`.

```bash
sudo chmod 600 /etc/ddclient.conf                        # contiene el token
sudo ddclient -daemon=0 -debug -verbose -noquiet         # una ejecución de prueba, mostrando qué hace
sudo systemctl enable --now ddclient
```

Si el router de casa trae cliente DDNS compatible con Cloudflare, puedes usarlo en lugar de `ddclient`, porque el router conoce su IP de la WAN directamente. Usa solo uno de los dos, para que no se pisen.

**d) Comprobación.** Cuando cambie la IP de casa (puedes forzarlo reiniciando el router, si tu operador la reasigna), `dig +short torre.tudominio.com` debe mostrar la nueva en unos minutos. Los registros de `ddclient` están en `journalctl -u ddclient`.

**¿Deja salir la red de la universidad UDP 51820?** Muchas redes corporativas o universitarias solo permiten salida a puertos conocidos. No lo sabrás hasta probar el *handshake* (§1.1). Si no hay handshake y todo lo demás está bien, cambia el puerto de escucha de la torre a **UDP 443**, que suele estar permitido porque lo usa HTTP/3 (QUIC). Cambia también el reenvío del router y la regla del cortafuegos.

**En la torre y en el portátil:**

```bash
ip route get 1.1.1.1      # interfaz física de salida (enp3s0, enp0s31f6…) e IP local
ip route                  # que 10.100.0.0/24 y 10.200.0.0/24 no las use ya nada (si chocan, cambia de subred en todo el documento)
```

Apunta `IFACE_TORRE` e `IP_LOCAL_TORRE` (la IP de la torre en la LAN de casa).

Si el portátil tiene el Wi-Fi encendido además del cable, la salida a internet la decide la ruta por defecto. WireGuard funciona por cualquiera de los dos, así que no hace falta apagar nada.

**En la torre**, apunta también qué hipervisor usas (libvirt/QEMU, VirtualBox o VMware), porque §1.4 depende de él.

### 1.1 WireGuard

Instala `wireguard-tools` en las dos máquinas (Arch: `sudo pacman -S wireguard-tools`).

**Claves.** En cada máquina:

```bash
umask 077
wg genkey | tee wg.key | wg pubkey > wg.pub
```

- La clave privada nunca sale de su máquina. La pública se copia al otro extremo.
- `umask 077` hace que los archivos solo los pueda leer tu usuario.

**Router de casa.** Reenvía **UDP 51820** externo a `IP_LOCAL_TORRE:51820`. Para que esa IP no cambie, fíjala en el DHCP del router (reserva por MAC). Es el **único** puerto que se abre a internet. El 4789 no se reenvía nunca.

**Cortafuegos de la torre:**

```bash
sudo ufw allow in on IFACE_TORRE to any port 51820 proto udp    # si usas UFW
```

**Torre**, `/etc/wireguard/wg0.conf` (permisos 600):

```ini
[Interface]
Address = 10.100.0.1/24
ListenPort = 51820
PrivateKey = <contenido de wg.key de la torre>
MTU = 1420

[Peer]
# Portátil
PublicKey = <contenido de wg.pub del portátil>
AllowedIPs = 10.100.0.2/32
```

**Portátil**, `/etc/wireguard/wg0.conf` (permisos 600):

```ini
[Interface]
Address = 10.100.0.2/24
PrivateKey = <contenido de wg.key del portátil>
MTU = 1420

[Peer]
# Torre
PublicKey = <contenido de wg.pub de la torre>
Endpoint = torre.tudominio.com:51820
AllowedIPs = 10.100.0.1/32
PersistentKeepalive = 25
```

Qué hace cada línea y por qué:

- **`Address`**: la IP del extremo dentro del túnel. Estas IPs fijas son las que usará VXLAN.
- **`ListenPort`** solo en la torre: es quien recibe conexiones. El portátil usa un puerto aleatorio de salida, porque está detrás de un NAT que no controlas.
- **`AllowedIPs`** tiene dos funciones a la vez:
  - Hacia fuera, decide qué tráfico entra en el túnel. Con solo la IP del otro extremo (`/32`), por `wg0` solo viaja el tráfico entre portátil y torre, no todo tu tráfico de internet, y el resto de tu conexión no cambia.
  - Hacia dentro, filtra: la torre solo acepta del portátil paquetes con origen 10.100.0.2.
- **`Endpoint`** solo en el portátil: dónde está la torre. La torre no necesita saber dónde está el portátil, porque lo aprende del primer paquete autenticado que recibe y lo actualiza si el portátil cambia de red.
  - **WireGuard resuelve el nombre una sola vez, al ejecutar `wg-quick up`.** Si la IP de casa cambia con el túnel levantado, el portátil sigue enviando a la IP vieja y se pierde la conexión, aunque Cloudflare ya tenga la nueva.
  - En la fase 1 se arregla a mano con `sudo wg-quick down wg0 && sudo wg-quick up wg0`. En la fase 2 lo hace el script (§2.3, `reresolve_endpoint`).
- **`PersistentKeepalive = 25`**: el portátil envía un paquete cada 25 s. Sin esto, el NAT de la universidad cierra el mapeo tras un rato sin tráfico y la torre ya no puede enviar nada al portátil. VXLAN necesita que la torre pueda iniciar tráfico (por ejemplo, el multicast de SPDP).
- **`MTU = 1420`**: explícito para que sea fácil de cambiar si la medida de §1.2 falla.

Levántalo en las dos máquinas (no persistente; desaparece al reiniciar):

```bash
sudo wg-quick up wg0
```

**Verificación:**

```bash
sudo wg show            # en el portátil y en la torre: "latest handshake: hace X segundos"
ping -c 3 10.100.0.1    # desde el portátil
ping -c 3 10.100.0.2    # desde la torre: comprueba que la torre puede iniciar tráfico hacia el portátil
```

Si no hay handshake:

```bash
# En la torre: ¿llega algo al puerto?
sudo tcpdump -ni IFACE_TORRE udp port 51820
```

- **No llega nada:** compara `dig +short torre.tudominio.com` con tu IP pública real; si no coinciden, o es una IP de Cloudflare, revisa §1.0.1. Si coinciden, el fallo está en el reenvío del router, el CG-NAT, o la red de la universidad que bloquea el puerto (prueba con 443, §1.0).
- **Llega pero no hay handshake:** las claves están cruzadas. Comprueba que cada `PublicKey` es la del *otro* extremo.

**Probar desde casa.** Si un día pruebas con el portátil conectado a la misma red que la torre, el `Endpoint` con la IP pública depende de que el router haga *hairpin NAT*, y muchos no lo hacen. En ese caso, cambia temporalmente el `Endpoint` a `IP_LOCAL_TORRE:51820`.

### 1.2 Medir el MTU del túnel

```bash
# Desde el portátil: 1392 + 28 de cabeceras IP/ICMP = 1420, un paquete lleno de wg0
ping -c 3 -M do -s 1392 10.100.0.1
```

`-M do` prohíbe fragmentar. Si el paquete no cabe por algún punto del camino, no llega.

- **Responde:** `wg0` = 1420 y overlay = **1370**.
- **No responde, aunque el ping normal sí:** el camino tiene un MTU menor. Baja `MTU` en los dos `wg0.conf` (prueba 1380 y vuelve a medir con `-s 1352`), reinicia el túnel (`wg-quick down wg0 && wg-quick up wg0`) y usa como MTU del overlay el de `wg0` − 50 en el resto del documento.

Los comandos siguientes usan **1370**. Si has tenido que bajarlo, sustitúyelo en todos.

### 1.3 Torre: bridge y extremo VXLAN

```bash
# 1. Switch virtual, con multicast snooping desactivado
sudo ip link add br-swarm type bridge mcast_snooping 0
sudo ip link set br-swarm mtu 1370
sudo ip addr add 10.200.0.1/24 dev br-swarm
sudo ip link set br-swarm up
```

- `type bridge` crea un switch Ethernet por software. Todo lo que se conecte (el túnel y las VMs) queda en el mismo dominio de broadcast.
- `mcast_snooping 0`: por defecto, el bridge "escucha" los mensajes IGMP para enviar cada grupo multicast solo a los puertos interesados. Sin un *querier* IGMP en la red (aquí no hay ninguno), esas entradas caducan a los pocos minutos y el multicast llega de forma intermitente. El síntoma sería que los drones se descubren a veces. Con el snooping desactivado, el bridge reenvía el multicast a todos los puertos, que es lo que queremos con tan pocas máquinas.
- `mtu 1370`: fijado a mano, el bridge no lo recalcula al añadir puertos.
- La IP 10.200.0.1 en el bridge sirve para que el **host** de la torre también esté en el overlay. Es útil para hacer ping y diagnosticar, aunque no ejecute drones.

```bash
# 2. Extremo VXLAN apuntando al portátil a través de WireGuard
sudo ip link add vxlan0 type vxlan id 42 dstport 4789 \
     local 10.100.0.1 remote 10.100.0.2 dev wg0
sudo ip link set vxlan0 mtu 1370
sudo ip link set vxlan0 master br-swarm
sudo ip link set vxlan0 up
```

- `id 42`: el VNI. Los dos extremos deben usar el mismo, porque es como el número de VLAN del túnel.
- `dstport 4789`: el puerto estándar de IANA. **Ponlo siempre**, porque el valor por defecto de Linux es 8472 (heredado de antes del estándar) y, si un extremo lo omite, no se entienden.
- `local 10.100.0.1 remote 10.100.0.2 dev wg0`: los paquetes VXLAN salen con origen y destino en las IPs de WireGuard, así que el kernel los mete en `wg0`, que los cifra. Como esas IPs no cambian nunca, la configuración de la torre tampoco cambia aunque el portátil vaya de una red a otra.
- `remote` es **imprescindible**: es el destino del tráfico broadcast y multicast (ARP, SPDP). Sin él, VXLAN solo aprende destinos de tráfico unicast y descarta el multicast, justo el que necesita DDS.
- `master br-swarm`: enchufa el túnel al switch. A partir de aquí, una trama que entra por `vxlan0` llega a las VMs y viceversa.
- `vxlan0` no tiene IP, porque es un puerto del switch, no un host. La IP del host de la torre está en el bridge.

**Verificación:**

```bash
ip -d link show vxlan0         # "vxlan id 42 remote 10.100.0.2 local 10.100.0.1 dev wg0 … dstport 4789"
bridge link show               # vxlan0 con "master br-swarm"
cat /sys/class/net/br-swarm/bridge/multicast_snooping   # 0
```

### 1.4 Torre: cortafuegos del overlay

Hay tres tipos de tráfico, y cada uno puede bloquearse por separado:

**a) VXLAN dentro del túnel** (UDP 4789 que entra por `wg0`):

```bash
sudo ufw allow in on wg0 to any port 4789 proto udp
```

**b) Las tramas que cruzan el bridge (VM ↔ vxlan0):**

```bash
sysctl net.bridge.bridge-nf-call-iptables 2>/dev/null
```

Si devuelve `1` (`br_netfilter` cargado, cosa que hacen Docker y algunas instalaciones de libvirt), el tráfico entre puertos del bridge pasa por la cadena FORWARD. Docker pone esa cadena en DROP, y UFW también por defecto. El resultado sería que el ping al host 10.200.0.1 funciona y el ping a las VMs desde el portátil no. Permítelo:

```bash
# Con UFW:
sudo ufw route allow in on br-swarm out on br-swarm
# Con iptables a pelo / Docker:
sudo iptables -I FORWARD -i br-swarm -o br-swarm -j ACCEPT
```

**c) DDS hacia el propio host** (solo si vas a ejecutar nodos ROS en el host de la torre):

```bash
sudo ufw allow in on br-swarm
```

Si no hay cortafuegos activo (`sudo ufw status`, `sudo nft list ruleset`), sáltate este paso, pero compruébalo en lugar de suponerlo.

Las reglas de UFW **son persistentes**: se quedan aunque reinicies.

### 1.5 Torre: conectar las VMs al bridge

Cada VM necesita una NIC conectada a `br-swarm`.

**libvirt / virt-manager.** En la VM, *Añadir hardware → Red → Origen: dispositivo puente, nombre `br-swarm`, modelo virtio*. Equivale a este XML (`sudo virsh edit VM`):

```xml
<interface type='bridge'>
  <source bridge='br-swarm'/>
  <model type='virtio'/>
  <mtu size='1370'/>
</interface>
```

- `<mtu size='1370'/>` hace que libvirt cree la interfaz tap con ese MTU y que, con virtio, el invitado lo reciba automáticamente.
- **El bridge debe existir antes de arrancar la VM.** Si no, libvirt se niega a arrancarla. Esto importa en la fase 2.

**VirtualBox.** *Configuración → Red → Adaptador puente → Nombre: `br-swarm`*. **VMware**: un *vmnet* en modo puente vinculado a `br-swarm` con `vmware-netcfg`.

**¿Quitar la NIC NAT de la VM?** Muchas VMs tienen otra NIC con NAT (192.168.122.x en libvirt) para salir a internet. DDS anuncia **todas** las IPs de la máquina a los demás participantes, y el portátil intentará hablar con la VM por una 192.168.122.x que no puede alcanzar. El síntoma típico es que `ros2 topic list` ve los topics pero no llega ningún dato. Hay dos opciones:

1. Instala todo lo necesario en la VM (pixi y el proyecto compilado), y luego deja solo la NIC de `br-swarm`. Es lo más sencillo.
2. Mantén la NIC NAT y restringe Fast DDS a la subred 10.200.0.0/24 con un perfil XML. Esto es parte de la issue #8.

Lo mismo pasa en el portátil: DDS anunciará también su IP del cable y la de `wg0`. Las VMs no pueden alcanzar ninguna de las dos, y Fast DDS suele acabar usando la que sí responde, pero si ves el síntoma anterior, el perfil de la issue #8 también lo resuelve.

**Dentro de cada VM:**

```bash
ip link                                   # localiza la interfaz nueva (ens3, enp1s0…)
sudo ip addr add 10.200.0.11/24 dev ens3  # .12, .13… en las siguientes
sudo ip link set ens3 mtu 1370
sudo ip link set ens3 up
```

- IPs estáticas porque en el overlay no hay servidor DHCP.
- El MTU tiene que coincidir con el del resto del overlay. Si la VM envía tramas mayores, el bridge las descarta al pasarlas a `vxlan0`.
- Si la VM usa NetworkManager, puede quitar la IP de una interfaz que considera suya. Si te ocurre, ejecuta `nmcli device set ens3 managed no` antes de asignarla.
- Si la VM tiene cortafuegos, permite la entrada por `ens3`.

**Verificación (desde el host de la torre):**

```bash
ping -c 2 10.200.0.11
ping -c 2 -M do -s 1342 10.200.0.11     # 1342 + 28 = 1370: paquete lleno sin fragmentar
bridge link show                        # vnetX (o vboxnet…) con master br-swarm
```

### 1.6 Portátil: extremo VXLAN

```bash
sudo ip link add vxlan0 type vxlan id 42 dstport 4789 \
     local 10.100.0.2 remote 10.100.0.1 dev wg0
sudo ip link set vxlan0 mtu 1370
sudo ip addr add 10.200.0.2/24 dev vxlan0
sudo ip link set vxlan0 up

# Multicast por el overlay
sudo ip route add 224.0.0.0/4 dev vxlan0
```

- Aquí no hay bridge: el portátil es un solo host, así que la IP va directamente en `vxlan0`.
- Es la configuración simétrica a la torre: todo el broadcast y multicast va hacia 10.100.0.1, dentro de `wg0`.
- **La ruta 224.0.0.0/4.** Un programa que envía multicast sin elegir interfaz (como `ros2 multicast send`) lo saca por la ruta por defecto, es decir, por el cable, no por el overlay. Con esta ruta, el multicast sale por `vxlan0`. Fast DDS envía por todas las interfaces, así que no la necesita estrictamente, pero las pruebas de §1.7 sí.

**Cortafuegos del portátil (Arch).** Comprueba qué tienes (`sudo nft list ruleset`, `ufw status` o `firewall-cmd --state`). No hay que abrir nada en la interfaz del cable: es el portátil quien inicia WireGuard, y las respuestas entran como tráfico de una conexión ya establecida. Dentro de los túneles sí:

```bash
sudo ufw allow in on wg0 to any port 4789 proto udp   # VXLAN que llega de la torre
sudo ufw allow in on vxlan0                           # DDS dentro del overlay
```

DDS usa puertos unicast que dependen del dominio y del número de participante (7400 + …). Abrir la interfaz `vxlan0` entera es más sencillo y no expone nada fuera del overlay.

**Verificación:**

```bash
ping -c 3 10.200.0.1                     # host de la torre: VXLAN sobre WireGuard funciona
ping -c 3 10.200.0.11                    # VM: el bridge y el cortafuegos FORWARD funcionan
ping -c 2 -M do -s 1342 10.200.0.11      # MTU de extremo a extremo
ip neigh show dev vxlan0                 # MACs de la torre y de las VMs aprendidas por ARP
```

Si falla el ping a .1 (con WireGuard funcionando según §1.1):

```bash
# En la torre: ¿llega VXLAN por el túnel?
sudo tcpdump -ni wg0 udp port 4789
```

- Si no llega nada, revisa el cortafuegos de salida del portátil o el `local`/`remote` del portátil.
- Si llegan paquetes pero no hay respuesta, revisa el VNI y el `dstport` en los dos extremos con `ip -d link show vxlan0`, y la regla §1.4a.

Si .1 responde y .11 no, el problema está en §1.4b o §1.5.

### 1.7 Verificación de multicast y de DDS

**Multicast genérico:**

```bash
# En una VM:
ros2 multicast receive
# En el portátil:
ros2 multicast send
```

En la VM debe salir `Received from 10.200.0.2`. Esta prueba usa el grupo 225.0.0.1 y no el de SPDP, así que solo demuestra que el multicast cruza los túneles y el bridge, no que DDS funcione.

**DDS real, con el proyecto:**

```bash
# Las dos máquinas con el mismo ROS_DOMAIN_ID (o ninguna lo define) y sin
# ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST:
echo "$ROS_DOMAIN_ID $ROS_AUTOMATIC_DISCOVERY_RANGE"

# En el portátil:
pixi run fake-hand animated
# En la VM 1:
pixi run drone 1
# En la VM 2:
pixi run drone 2
# En el portátil:
ros2 topic list                     # aparecen /swarm/state y /hand/landmarks
ros2 topic echo /swarm/state --once # llegan datos de los drones, no solo el nombre del topic
pixi run swarm-view                 # vista del enjambre: publica /swarm/markers a partir de /swarm/state
pixi run rviz                       # (o rviz-arch) los drones se mueven, sin parpadeo
```

Los drones solo publican su estado. Las esferas que pinta RViz2 las publica la vista del enjambre en el propio portátil, así que sin `pixi run swarm-view` RViz2 muestra la mano pero no los drones.

`ROS_DOMAIN_ID` no elige la interfaz de red. Solo separa enjambres lógicos, y tiene que coincidir en todas las máquinas.

Si `topic list` muestra los topics pero `echo` no recibe datos, el problema son los locators unicast (§1.5, la NIC NAT de la VM) o el cortafuegos de `vxlan0`.

Por internet hay más latencia que en una LAN. Si los drones del portátil y los de la torre se ven con retraso, compara el tiempo de `ping 10.200.0.11` con `neighbor_timeout`: el timeout tiene que ser bastante mayor que la latencia y que sus picos.

### 1.8 Desmontar

```bash
# Portátil
sudo ip link del vxlan0            # borra también su IP y la ruta 224.0.0.0/4
sudo wg-quick down wg0

# Torre (apaga antes las VMs o desconéctalas)
sudo ip link del vxlan0
sudo ip link del br-swarm
sudo wg-quick down wg0

# VM
sudo ip addr flush dev ens3
```

Se quedan, porque son persistentes: los `wg0.conf`, las reglas de UFW y el reenvío del router. Es lo que quieres para la fase 2.

---

## Fase 2: script para levantar la red

Objetivo: un único script, `scripts/vxlan.sh`, que haga exactamente lo de la fase 1 sin errores de tecleo, que se pueda ejecutar varias veces sin romper nada y que sepa tirar la red y diagnosticarla. **No escribas el script hasta que la fase 1 funcione de principio a fin**, porque el script solo automatiza comandos que ya sabes que funcionan.

Lo que queda fuera del script, porque ya es persistente y se configura una sola vez: las claves y los `wg0.conf`, el reenvío del router, el registro de Cloudflare con `ddclient` y las reglas de UFW. El script **usa** WireGuard (lo levanta si no está), pero no lo configura.

### 2.1 Interfaz del script

```
sudo scripts/vxlan.sh <rol> <acción>

  rol:     torre | portatil | vm
  acción:  up | down | status
```

- **Un script con roles en lugar de tres scripts.** Comparten la configuración (VNI, puerto, subredes, MTU) y casi todas las funciones. Si cambias el MTU, lo cambias en un solo sitio.
- **`up`, `down` y `status`.** `down` permite volver a un estado limpio, y `status` te ahorra recordar los comandos de verificación.

### 2.2 Configuración separada del código

Crea un archivo `scripts/vxlan.env`, que el script carga con `source`:

```bash
VNI=42
VXLAN_PORT=4789
MTU=1370                 # MTU de wg0 − 50
BRIDGE=br-swarm
WG_IFACE=wg0

TORRE_WG_IP=10.100.0.1
PORTATIL_WG_IP=10.100.0.2
TORRE_HOST=torre.tudominio.com   # nombre en Cloudflare (§1.0.1)
WG_PORT=51820
TORRE_WG_PUBKEY=<clave pública de la torre>   # pública: no es un secreto
TORRE_OVERLAY_IP=10.200.0.1
PORTATIL_OVERLAY_IP=10.200.0.2

# Solo para el rol vm
VM_IFACE=ens3
VM_OVERLAY_IP=10.200.0.11
```

- **Por qué un archivo aparte.** Si un día cambias de subred o el MTU, solo tocas este archivo, no el código.
- Con WireGuard, todas las IPs del script son fijas. No hace falta detectar la IP del cable, ni volver a ejecutar nada en la torre cuando el portátil cambia de red.
- Aquí no hay secretos: las claves están en `/etc/wireguard/`. Por eso este archivo se puede versionar. **Nunca** versiones los `wg0.conf`.

### 2.3 Esqueleto y funciones

Estructura recomendada (la implementación es tuya):

```bash
#!/usr/bin/env bash
set -euo pipefail
```

- `-e` corta en el primer comando que falle, para no seguir configurando sobre un paso roto.
- `-u` convierte en error usar una variable no definida, por ejemplo una errata en `vxlan.env`.
- `-o pipefail` hace que un fallo dentro de una tubería también cuente como fallo.

Funciones, en el orden en que se usan:

| Función | Qué hace | Por qué |
|---|---|---|
| `require_root` | Sale con un error si `$EUID != 0`. | Todos los `ip link` y `wg-quick` necesitan root. Es mejor un mensaje claro al principio que un fallo a mitad que deje la red a medias. |
| `load_config` | Carga `vxlan.env` desde el directorio del script (`$(dirname "$0")`) y comprueba que existen las variables necesarias para el rol. | Que funcione aunque lo llames desde otro directorio y que falle pronto si falta algo. |
| `ensure_wg` (torre, portátil) | Si `wg0` no existe (`ip link show wg0`), ejecuta `wg-quick up wg0`. Después espera unos segundos a que haya handshake (`wg show wg0 latest-handshakes` distinto de 0) y, si no lo hay, avisa sin abortar. | VXLAN sobre un WireGuard caído "se levanta" sin errores pero no transporta nada. Mejor saberlo aquí. En la torre no se aborta porque el handshake solo llega cuando el portátil se conecta. |
| `reresolve_endpoint` (portátil) | Resuelve `torre.tudominio.com` (`getent ahostsv4`) y lo compara con la IP del endpoint actual (`wg show wg0 endpoints`). Si difieren, ejecuta `wg set wg0 peer <clave pública de la torre> endpoint <nombre>:51820`. Se llama en `up` y en `status`. | WireGuard solo resuelve el nombre al levantar el túnel (§1.1). `wg set` cambia el endpoint **sin tirar el túnel**, así que no se interrumpen las conexiones abiertas. Es lo mismo que hace `reresolve-dns.sh`, que trae `wireguard-tools` (en Arch, en `/usr/share/wireguard-tools/examples/reresolve-dns/`). Puedes llamarlo en lugar de escribir la función. |
| `ensure_bridge` (torre) | Si `br-swarm` no existe, lo crea con `mcast_snooping 0`. Siempre aplica MTU, IP y `up`. | Idempotencia (ver §2.4). |
| `ensure_vxlan` (torre, portátil) | Si `vxlan0` existe, compara su configuración (`ip -d -j link show vxlan0`) con la deseada: VNI, `local`, `remote`, puerto y `dev`. Si coincide, solo asegura MTU, `master` o IP y `up`. Si difiere, la borra y la recrea. | Los parámetros de VXLAN **no se pueden cambiar** con `ip link set`: hay que recrear la interfaz. Además, si `wg0` se recrea, `vxlan0` puede quedar asociada a la interfaz antigua, así que comprobar `dev` evita un túnel zombi. |
| `ensure_mcast_route` (portátil) | `ip route replace 224.0.0.0/4 dev vxlan0`. | `replace` en lugar de `add` no falla si la ruta ya existe. |
| `check_firewall` | Si UFW está activo, comprueba que existen las reglas de §1.4 (torre) o §1.6 (portátil) con `ufw status` y avisa de las que falten. | Las reglas son persistentes, así que no hay que repetirlas, pero un aviso evita olvidarlas en una máquina nueva. Si prefieres que el script las añada, que sea con un `--fix` explícito: un script de red no debería cambiar el cortafuegos sin que lo pidas. |
| `check_br_netfilter` (torre) | Si `net.bridge.bridge-nf-call-iptables` vale 1, avisa de la regla FORWARD de §1.4b. | Es el fallo más traicionero: todo parece bien y las VMs no responden. |
| `vm_up` (vm) | Asigna la IP (`ip addr replace`), el MTU y hace `up` en `VM_IFACE`. | `addr replace` es idempotente; `addr add` falla si la IP ya está. |
| `do_down` | Borra en orden inverso: ruta multicast y `vxlan0`, después `br-swarm`. Cada borrado solo si la interfaz existe. Deja `wg0` levantado, salvo con un `--all` explícito. | `down` dos veces seguidas no debe dar error. `wg0` se deja porque puede que lo estés usando para otra cosa (por ejemplo, SSH a la torre), y tirarlo desde lejos te deja sin acceso. |
| `do_status` | Por capas: `wg show` (handshake y bytes transferidos), ping a la otra IP de WireGuard, `ip -d link show` del túnel y del bridge, `bridge link show`, MTU, valor de snooping, `ip neigh show dev …` y ping a la otra punta del overlay con `-M do -s $((MTU-28))`. | Es la verificación de la fase 1 en un comando, en el mismo orden. La primera capa que falla indica dónde está el problema. |

Cierra el script con un `case "$ROL:$ACCION"` que llame a las funciones adecuadas, y una ayuda de uso si los argumentos no son válidos.

### 2.4 Idempotencia: la regla más importante del script

*Idempotente* significa que ejecutarlo una o diez veces deja el sistema igual. Hace falta porque vas a ejecutar `up` cuando la red está a medias, después de que se caiga el túnel o "por si acaso". Las técnicas que necesitas:

- **Crear solo si no existe:** `ip link show X &>/dev/null || ip link add X …`.
- **Comandos que ya son idempotentes:** `ip link set … mtu/up/master`, `ip addr replace` e `ip route replace`.
- **Lo que no se puede modificar se recrea**: los parámetros de `vxlan0`.
- **Borrar solo si existe:** `ip link show X &>/dev/null && ip link del X`.

Prueba del script: ejecuta `up` dos veces, `down` dos veces y `up` otra vez. Ninguna ejecución debe dar error y `status` debe pasar al final.

### 2.5 Persistencia con systemd (opcional, solo en la torre)

La torre es la que siempre debe estar disponible, así que tiene sentido que levante todo al arrancar.

**WireGuard:**

```bash
sudo systemctl enable --now wg-quick@wg0
```

`wg-quick@` es una unidad que ya trae `wireguard-tools`: lee `/etc/wireguard/wg0.conf` al arrancar.

**Overlay:**

```ini
# /etc/systemd/system/swarm-net.service
[Unit]
Description=Overlay VXLAN del enjambre
After=wg-quick@wg0.service
Requires=wg-quick@wg0.service
Before=libvirtd.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/ruta/al/repo/scripts/vxlan.sh torre up
ExecStop=/ruta/al/repo/scripts/vxlan.sh torre down

[Install]
WantedBy=multi-user.target
```

- `After`/`Requires=wg-quick@wg0`: `vxlan0` se crea con `dev wg0`, así que `wg0` tiene que existir antes. Si `wg0` falla, el overlay no se intenta levantar.
- `Before=libvirtd.service`: el bridge tiene que existir antes de que libvirt autoarranque las VMs (§1.5).
- `oneshot` + `RemainAfterExit`: el script termina enseguida, pero systemd considera el servicio activo, de modo que `systemctl stop` ejecuta el `down`.
- Actívalo con `sudo systemctl daemon-reload && sudo systemctl enable --now swarm-net`.

**En la torre, también `ddclient`** (§1.0.1c), que ya se habilitó con `systemctl enable`. No depende del overlay, así que no necesita orden respecto a los otros servicios.

**Volver a resolver el nombre en el portátil (opcional).** Si dejas el túnel levantado mucho tiempo, un timer de systemd puede llamar a `reresolve_endpoint` cada minuto. Si el cambio de IP es raro, basta con ejecutar `vxlan.sh portatil status`, que ya lo hace.

**En el portátil no recomiendo un servicio de arranque.** Ejecuta `sudo scripts/vxlan.sh portatil up` cuando vayas a trabajar y `down` cuando acabes: así WireGuard no intenta conectar a casa desde cualquier red en la que estés. En las VMs, lo natural es fijar la IP estática con la herramienta de red de su distro (netplan, NetworkManager o systemd-networkd), y el rol `vm` del script queda para pruebas rápidas.

### 2.6 Lista de comprobación final

- [ ] `dig +short torre.tudominio.com` devuelve la IP pública de casa (no una de Cloudflare) y `ddclient` la actualiza tras un cambio.
- [ ] Desde la red de la universidad, `sudo wg show` en el portátil muestra un handshake reciente.
- [ ] `status` pasa en la torre y en el portátil, incluidos los pings de paquete lleno de `wg0` y del overlay.
- [ ] `ros2 multicast receive` en una VM recibe lo que envía el portátil.
- [ ] Con `pixi run fake-hand animated` en el portátil y dos drones en dos VMs distintas, `ros2 topic echo /swarm/state` en el portátil recibe datos de los dos.
- [ ] Con `pixi run swarm-view` en el portátil, RViz2 muestra el enjambre siguiendo la mano, sin parpadeo.
- [ ] Tras reiniciar la torre (si usas systemd), WireGuard, el bridge y las VMs vuelven sin intervención, y `portatil up` reconecta.
- [ ] Desenchufar el cable del portátil y volver a enchufarlo (o cambiar de red) recupera la conexión sin tocar la torre.
- [ ] Tras un cambio de IP de casa, `vxlan.sh portatil status` actualiza el endpoint y el handshake vuelve sin `wg-quick down/up`.

---

## A. Alternativa: Tailscale en lugar de WireGuard + Cloudflare

Tailscale es una red privada construida sobre WireGuard. La diferencia con el WireGuard manual es que un servidor de coordinación de Tailscale reparte las claves y ayuda a las máquinas a encontrarse y a atravesar los NAT. Con esta opción, **solo cambia la capa que une el portátil y la torre**. VXLAN, el bridge, las VMs y DDS se montan igual, salvo el MTU, que es más pequeño.

### A.0 ¿Cuál elijo?

| | WireGuard + Cloudflare (principal) | Tailscale (alternativa) |
|---|---|---|
| Abrir un puerto en el router | Sí (UDP 51820) | No |
| Funciona con CG-NAT en casa | No | Sí |
| Funciona si la universidad bloquea UDP | Solo si el 443 está permitido | Sí, a través de sus relays (DERP) por HTTPS, con más latencia |
| Nombre DNS, `ddclient`, volver a resolver el endpoint | Necesario | No hace falta: cada máquina tiene una IP 100.x fija |
| Claves | Las generas y copias tú | Las gestiona Tailscale |
| MTU del overlay | 1370 | **1230** |
| Latencia con conexión directa | Base | Prácticamente igual |
| Depende de un servicio externo | No | Sí, para registrar las máquinas y coordinarlas (los datos van directos entre ellas) |
| Lo montas tú de principio a fin | Sí | No: la capa de túnel la hace Tailscale |

**Implementar es mucho más rápido con Tailscale, pero la latencia no baja.**

- Con conexión directa entre portátil y torre, es prácticamente la misma que con WireGuard. En Linux, Tailscale cifra en espacio de usuario y gasta algo más de CPU, pero con el tráfico de este proyecto no se nota.
- Si no consigue conexión directa y pasa por un relay, la latencia sube bastante. Aun así, en esa misma red el WireGuard manual ni siquiera conectaría.

**Antes de elegir Tailscale, confirma con el profesor** que vale usar un servicio gestionado para la capa del túnel. La parte de capa 2 (VXLAN y bridge) la sigues montando tú en los dos casos.

**¿Y CoreDNS?** No aporta nada a este montaje. `ip link add … type vxlan` solo acepta IPs, no nombres, y las IPs 100.x de Tailscale no cambian. Si quieres nombres para hacer SSH o ping, MagicDNS ya te los da. No hace falta cambiar tu CoreDNS: puede seguir como está.

### A.1 Qué se sustituye y qué no

| Sección del camino principal | Con Tailscale |
|---|---|
| §1.0 Comprobaciones previas | Solo la comprobación de subredes y del hipervisor. CG-NAT, reenvío de puertos y bloqueo de UDP dejan de importar (pero afectan a la latencia, §A.4). |
| §1.0.1 Cloudflare y `ddclient` | **Se elimina.** |
| §1.1 WireGuard | **Se sustituye** por §A.2 y §A.3. |
| §1.2 Medir el MTU | **Se sustituye** por §A.5. |
| §1.3 a §1.8 (bridge, cortafuegos, VMs, portátil, verificación, desmontar) | **Igual**, cambiando las IPs, la interfaz y el MTU según §A.6. |
| Fase 2 | Cambios en §A.7. |

### A.2 Las dos máquinas en el tailnet

Si ya tienes Tailscale montado, la torre y el portátil probablemente ya están dentro. Compruébalo en cada una:

```bash
tailscale status      # lista de máquinas del tailnet y su estado
tailscale ip -4       # IP 100.x de esta máquina
```

Apunta `TS_IP_TORRE` y `TS_IP_PORTATIL`. Si alguna no está en el tailnet, instala Tailscale (Arch: `sudo pacman -S tailscale`, `sudo systemctl enable --now tailscaled`) y ejecuta `sudo tailscale up`.

**Por qué las IPs 100.x sirven para VXLAN:** Tailscale asigna a cada máquina una IP dentro del rango 100.64.0.0/10, que se mantiene mientras la máquina siga registrada en el tailnet, aunque cambie de red. Es lo mismo que te daban las IPs fijas de `wg0` (10.100.0.x).

**Desactiva la caducidad de la clave de la torre.** En la consola de administración de Tailscale, *Machines → la torre → … → Disable key expiry*. Por defecto, cada máquina tiene que volver a autenticarse cada 180 días. Una torre sin pantalla que nadie mira se caería del tailnet un día cualquiera, sin avisar. En el portátil puedes dejar la caducidad, porque si caduca te das cuenta y vuelves a iniciar sesión.

**Solo la torre y el portátil llevan Tailscale.** Las VMs no: llegan al portátil por el bridge, que lleva sus tramas a `vxlan0`, y este las envía por `tailscale0`.

### A.3 ACLs del tailnet

Las ACLs de Tailscale deciden qué máquina puede hablar con cuál. Mira tu política en la consola (*Access controls*):

- **Si es la política por defecto (todo permitido)**, no hay que hacer nada.
- **Si la has restringido**, permite el tráfico entre la torre y el portátil. Lo más sencillo es permitirlo todo entre las dos, porque ambas son tuyas y así también funcionan el ping y el SSH de diagnóstico:

```jsonc
{
  "hosts": {
    "torre":    "TS_IP_TORRE",
    "portatil": "TS_IP_PORTATIL"
  },
  "acls": [
    // VXLAN (UDP 4789), ping y diagnóstico entre torre y portátil
    { "action": "accept", "src": ["torre", "portatil"], "dst": ["torre:*", "portatil:*"] }
    // … tus reglas existentes …
  ]
}
```

Si prefieres lo mínimo, limita el destino a `:4789` con `"proto": "udp"`, pero entonces los pings de diagnóstico entre las dos IPs 100.x dejarán de funcionar.

**No anuncies 10.200.0.0/24 como *subnet route*** de Tailscale, ni en la torre ni en ninguna otra máquina. En Linux, Tailscale instala sus rutas en una tabla propia (52) que se consulta **antes** que la tabla principal. Si anuncias la subred del overlay, el tráfico hacia las VMs iría por Tailscale en capa 3, saltándose VXLAN, y el multicast de DDS dejaría de funcionar. Lo mismo con 10.200.0.0/24 si ya la usa alguna ruta de tu tailnet: en ese caso cambia la subred del overlay en todo el documento.

### A.4 Conexión directa y latencia

```bash
# Desde el portátil, conectado a la red de la universidad:
tailscale ping TS_IP_TORRE     # dice si la conexión es directa o "via DERP(…)", y el tiempo
tailscale status               # en la línea de la torre: "direct" o "relay"
tailscale netcheck             # qué permite la red: UDP, tipo de NAT, relay más cercano
```

- **"direct"**: el tráfico va cifrado de punto a punto, igual que con WireGuard. Es lo que quieres.
- **"relay" / "via DERP"**: el tráfico rebota en un servidor de Tailscale. Funciona, pero añade latencia, y DDS lo notará.

Si sale relay, se puede ayudar a que conecte directo desde el lado de casa:

- Activa **UPnP o NAT-PMP** en el router de casa, que Tailscale usa para abrirse un puerto él solo.
- O reenvía en el router **UDP 41641** (el puerto por defecto de `tailscaled`) a la IP local de la torre. Este reenvío no es obligatorio: solo facilita la conexión directa.

Si la universidad bloquea UDP de salida, no hay forma de conseguir conexión directa desde allí. Tailscale funcionará por DERP y la WireGuard manual no funcionaría.

En cualquier caso, compara el tiempo de `tailscale ping` con `neighbor_timeout`: el timeout tiene que ser bastante mayor que la latencia y que sus picos.

### A.5 MTU

`tailscale0` tiene un MTU de **1280**, el mínimo de IPv6. Tailscale lo fija tan bajo para que sus paquetes quepan por cualquier camino sin que haya que medir nada. Con VXLAN encima:

| Capa | MTU |
|---|---|
| `tailscale0` | 1280 |
| Overlay (`br-swarm`, los dos `vxlan0`, NIC de cada VM, `<mtu>` de libvirt) | **1230** (1280 − 50) |

Medición (sustituye §1.2):

```bash
# Desde el portátil: 1252 + 28 = 1280, un paquete lleno de tailscale0
ping -c 3 -M do -s 1252 TS_IP_TORRE
```

Con Tailscale debería responder siempre, porque el MTU ya es conservador. Si no responde, la causa más probable son las ACLs (§A.3), no el MTU.

Un MTU más bajo significa algo más de fragmentación en los datagramas grandes de DDS. Con los mensajes de este proyecto (landmarks y estado de drones, unos cientos de bytes) no se nota.

### A.6 Cambios en los pasos §1.3 a §1.8

Todo se hace igual, con estas sustituciones:

| Donde pone | Con Tailscale pon |
|---|---|
| `dev wg0` | `dev tailscale0` |
| `local 10.100.0.1 remote 10.100.0.2` (torre) | `local TS_IP_TORRE remote TS_IP_PORTATIL` |
| `local 10.100.0.2 remote 10.100.0.1` (portátil) | `local TS_IP_PORTATIL remote TS_IP_TORRE` |
| `mtu 1370`, `<mtu size='1370'/>` | `mtu 1230`, `<mtu size='1230'/>` |
| `ping -M do -s 1342` | `ping -M do -s 1202` |
| Reglas de cortafuegos con `in on wg0` | `in on tailscale0` |
| `tcpdump -ni wg0 udp port 4789` | `tcpdump -ni tailscale0 udp port 4789` |
| `wg-quick down wg0` al desmontar | **No lo hagas**: no ejecutes `tailscale down`. Te quitaría el acceso al resto del tailnet, y probablemente el SSH a la torre. Para desmontar basta con borrar `vxlan0` y `br-swarm`. |

Ejemplo, en la torre:

```bash
sudo ip link add vxlan0 type vxlan id 42 dstport 4789 \
     local TS_IP_TORRE remote TS_IP_PORTATIL dev tailscale0
sudo ip link set vxlan0 mtu 1230
```

**Cortafuegos.** Tailscale añade sus propias cadenas a iptables/nftables (`ts-input`, `ts-forward`), pero no aceptan todo el tráfico de `tailscale0`. Si tienes UFW u otro cortafuegos, añade igualmente las reglas de §1.4a y §1.6 sobre `tailscale0`:

```bash
sudo ufw allow in on tailscale0 to any port 4789 proto udp
```

### A.7 Cambios en la fase 2 (script)

**`vxlan.env`**: el túnel pasa a ser configurable, de modo que el mismo script sirve para las dos opciones.

```bash
TUNNEL=tailscale            # wireguard | tailscale
TUNNEL_IFACE=tailscale0     # wg0 con wireguard
MTU=1230                    # 1370 con wireguard
TORRE_TUNNEL_IP=100.x.y.z   # TS_IP_TORRE   (10.100.0.1 con wireguard)
PORTATIL_TUNNEL_IP=100.a.b.c # TS_IP_PORTATIL (10.100.0.2 con wireguard)
# TORRE_HOST, WG_PORT y TORRE_WG_PUBKEY solo hacen falta con wireguard
```

- **Por qué no detectar las IPs 100.x con `tailscale ip`.** El script sí puede usar `tailscale ip -4` para la IP local, pero necesita también la del otro extremo como `remote`. Escribir las dos en el archivo es más simple y no cambian.

**Funciones:**

| Función del camino principal | Con `TUNNEL=tailscale` |
|---|---|
| `ensure_wg` → renómbrala `ensure_tunnel` | Comprueba que `tailscaled` está activo y que la máquina está conectada: `tailscale status --json` debe tener `"BackendState": "Running"`. Espera unos segundos a que `tailscale ip -4` devuelva la IP esperada. **No** ejecutes `tailscale up` ni `login` desde el script: si la sesión ha caducado, hace falta iniciar sesión en el navegador, cosa que un script no puede hacer. Avisa y sal con error. |
| `reresolve_endpoint` | No se usa: Tailscale encuentra la torre por sí mismo. |
| `do_down` | Nunca toca Tailscale, ni siquiera con `--all`. |
| `do_status` | Sustituye `wg show` por `tailscale status` y `tailscale ping -c 1 <otra IP 100.x>`, que también dice si la conexión es directa o por relay. El resto (VXLAN, bridge, MTU, pings del overlay) es igual. |

**systemd en la torre** (§2.5): no hace falta `wg-quick@wg0`, ni `ddclient`. `tailscaled` ya está habilitado si usas Tailscale. Cambia la dependencia del servicio:

```ini
After=tailscaled.service
Requires=tailscaled.service
```

`tailscaled` arranca antes de tener IP, porque tiene que conectarse al servidor de coordinación. Por eso `ensure_tunnel` tiene que **esperar** a que aparezca la IP 100.x, con un límite (por ejemplo, 60 s), en lugar de fallar en el primer intento. Si no, el servicio fallará en algunos arranques y en otros no.

### A.8 Lista de comprobación de la alternativa

En lugar de los dos primeros puntos de §2.6:

- [ ] `tailscale status` muestra la torre y el portátil en línea, y la torre tiene desactivada la caducidad de la clave.
- [ ] Desde la universidad, `tailscale ping TS_IP_TORRE` responde, idealmente con conexión directa, y la latencia es bastante menor que `neighbor_timeout`.
- [ ] `ping -M do -s 1252 TS_IP_TORRE` y `ping -M do -s 1202 10.200.0.11` responden desde el portátil.
- [ ] Ninguna máquina del tailnet anuncia 10.200.0.0/24 como subnet route.

El resto de la lista de §2.6 se aplica igual.
