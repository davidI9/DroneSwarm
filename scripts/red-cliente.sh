#!/usr/bin/env bash
# Red del enjambre en el portátil (cliente), con Tailscale como túnel.
# Monta el extremo VXLAN hacia la torre y la ruta multicast (VIRTUALNETWORK.md §A).
# No toca Tailscale ni las ACLs.
#
# Uso: sudo scripts/red-cliente.sh <ip_tailscale_portatil> <ip_tailscale_torre> [up|down|status]
set -euo pipefail

VNI=42
VXLAN_PORT=4789
TUNNEL_IFACE=tailscale0
MTU=1230                       # MTU de tailscale0 (1280) − 50 de VXLAN
VXLAN_IFACE=vxlan0
OVERLAY_IP=10.200.0.2/24
PEER_OVERLAY_IP=10.200.0.1
MCAST_ROUTE=224.0.0.0/4
TUNNEL_TIMEOUT=60              # segundos esperando a que Tailscale tenga IP

die() { echo "Error: $*" >&2; exit 1; }
warn() { echo "Aviso: $*" >&2; }
log() { echo "==> $*"; }

usage() {
    cat >&2 <<EOF
Uso: sudo $0 <ip_tailscale_portatil> <ip_tailscale_torre> [up|down|status]

  ip_tailscale_portatil  IP 100.x de esta máquina   (tailscale ip -4 en el portátil)
  ip_tailscale_torre     IP 100.x de la torre       (tailscale ip -4 en la torre)
  acción                 up (por defecto), down o status
EOF
    exit 1
}

is_ipv4() {
    [[ $1 =~ ^([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})$ ]] || return 1
    local octeto
    for octeto in "${BASH_REMATCH[@]:1}"; do
        (( octeto <= 255 )) || return 1
    done
}

[[ $# -ge 2 && $# -le 3 ]] || { echo "Error: faltan las IPs de Tailscale." >&2; usage; }
LOCAL_IP=$1
REMOTE_IP=$2
ACTION=${3:-up}

is_ipv4 "$LOCAL_IP"  || die "'$LOCAL_IP' no es una IPv4 válida (IP de Tailscale del portátil)."
is_ipv4 "$REMOTE_IP" || die "'$REMOTE_IP' no es una IPv4 válida (IP de Tailscale de la torre)."
[[ $LOCAL_IP != "$REMOTE_IP" ]] || die "la IP del portátil y la de la torre son la misma."

require_root() {
    [[ $EUID -eq 0 ]] || die "hace falta root: ejecútalo con sudo."
}

ts_running() {
    grep -Eq '"BackendState":[[:space:]]*"Running"' <<<"$(tailscale status --json 2>/dev/null)"
}

# Espera a que tailscaled esté conectado y tenga la IP local indicada.
# No ejecuta 'tailscale up': si la sesión ha caducado, hay que iniciarla a mano.
ensure_tunnel() {
    command -v tailscale &>/dev/null || die "tailscale no está instalado."
    log "Esperando a Tailscale (máx. ${TUNNEL_TIMEOUT}s)"
    local i
    for (( i = 0; i < TUNNEL_TIMEOUT; i++ )); do
        if ts_running && grep -qx "$LOCAL_IP" <<<"$(tailscale ip -4 2>/dev/null)"; then
            ip link show "$TUNNEL_IFACE" &>/dev/null || die "no existe la interfaz $TUNNEL_IFACE."
            return 0
        fi
        sleep 1
    done
    if ts_running; then
        die "Tailscale está conectado, pero la IP de esta máquina es '$(tailscale ip -4 2>/dev/null | head -n1)', no $LOCAL_IP."
    fi
    die "Tailscale no está conectado. Ejecuta 'sudo tailscale up' (o inicia sesión) y vuelve a probar."
}

# Los parámetros de VXLAN no se pueden cambiar en caliente: se recrea siempre.
# Sin bridge: el portátil es un solo host, así que la IP va en vxlan0.
ensure_vxlan() {
    if ip link show "$VXLAN_IFACE" &>/dev/null; then
        ip link del "$VXLAN_IFACE"
    fi
    log "Creando $VXLAN_IFACE (VNI $VNI, $LOCAL_IP -> $REMOTE_IP por $TUNNEL_IFACE)"
    ip link add "$VXLAN_IFACE" type vxlan id "$VNI" dstport "$VXLAN_PORT" \
        local "$LOCAL_IP" remote "$REMOTE_IP" dev "$TUNNEL_IFACE"
    ip link set "$VXLAN_IFACE" mtu "$MTU"
    ip addr replace "$OVERLAY_IP" dev "$VXLAN_IFACE"
    ip link set "$VXLAN_IFACE" up
}

# El multicast sin interfaz elegida (ros2 multicast send) sale por el overlay.
ensure_mcast_route() {
    ip route replace "$MCAST_ROUTE" dev "$VXLAN_IFACE"
}

# Solo avisa: un script de red no cambia el cortafuegos sin que se lo pidas.
check_firewall() {
    local estado
    command -v ufw &>/dev/null || return 0
    estado=$(ufw status 2>/dev/null) || return 0
    if grep -q "^Status: active" <<<"$estado"; then
        if ! grep -Eq "^${VXLAN_PORT}/udp on ${TUNNEL_IFACE}[[:space:]]+ALLOW" <<<"$estado"; then
            warn "UFW activo sin regla para VXLAN. Añádela con:"
            warn "  sudo ufw allow in on $TUNNEL_IFACE to any port $VXLAN_PORT proto udp"
        fi
        if ! grep -Eq "^Anywhere on ${VXLAN_IFACE}[[:space:]]+ALLOW" <<<"$estado"; then
            warn "UFW activo sin regla para DDS en el overlay. Añádela con:"
            warn "  sudo ufw allow in on $VXLAN_IFACE"
        fi
    fi
}

do_up() {
    ensure_tunnel
    ensure_vxlan
    ensure_mcast_route
    check_firewall
    log "Overlay listo: $VXLAN_IFACE ${OVERLAY_IP%/*}, MTU $MTU"
}

# Nunca toca Tailscale. Borrar vxlan0 borra también su IP y la ruta multicast.
do_down() {
    if ip link show "$VXLAN_IFACE" &>/dev/null; then
        log "Borrando $VXLAN_IFACE"
        ip link del "$VXLAN_IFACE"
    fi
}

do_status() {
    echo "--- Tailscale"
    tailscale status || true
    tailscale ping -c 1 "$REMOTE_IP" || true
    echo "--- Ping de paquete lleno a la torre por Tailscale"
    ping -c 2 -W 2 -M do -s 1252 "$REMOTE_IP" || true
    echo "--- $VXLAN_IFACE"
    ip -d link show "$VXLAN_IFACE" || true
    ip -4 addr show dev "$VXLAN_IFACE" 2>/dev/null || true
    ip route show "$MCAST_ROUTE" || true
    echo "--- Vecinos del overlay"
    ip neigh show dev "$VXLAN_IFACE" 2>/dev/null || true
    echo "--- Ping de paquete lleno a la torre por el overlay"
    ping -c 2 -W 2 -M do -s $(( MTU - 28 )) "$PEER_OVERLAY_IP" || true
}

case "$ACTION" in
    up)     require_root; do_up ;;
    down)   require_root; do_down ;;
    status) do_status ;;
    *)      echo "Error: acción desconocida '$ACTION'." >&2; usage ;;
esac
