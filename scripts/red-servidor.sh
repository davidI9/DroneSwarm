#!/usr/bin/env bash
# Red del enjambre en la torre (servidor), con Tailscale como túnel.
# Monta el bridge br-swarm y el extremo VXLAN hacia el portátil (VIRTUALNETWORK.md §A).
# No toca Tailscale, las VMs ni las ACLs.
#
# Uso: sudo scripts/red-servidor.sh <ip_tailscale_torre> <ip_tailscale_portatil> [up|down|status]
set -euo pipefail

VNI=42
VXLAN_PORT=4789
TUNNEL_IFACE=tailscale0
MTU=1230                       # MTU de tailscale0 (1280) − 50 de VXLAN
BRIDGE=br-swarm
VXLAN_IFACE=vxlan0
OVERLAY_IP=10.200.0.1/24
PEER_OVERLAY_IP=10.200.0.2
TUNNEL_TIMEOUT=60              # segundos esperando a que Tailscale tenga IP

die() { echo "Error: $*" >&2; exit 1; }
warn() { echo "Aviso: $*" >&2; }
log() { echo "==> $*"; }

usage() {
    cat >&2 <<EOF
Uso: sudo $0 <ip_tailscale_torre> <ip_tailscale_portatil> [up|down|status]

  ip_tailscale_torre     IP 100.x de esta máquina   (tailscale ip -4 en la torre)
  ip_tailscale_portatil  IP 100.x del portátil      (tailscale ip -4 en el portátil)
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

is_ipv4 "$LOCAL_IP"  || die "'$LOCAL_IP' no es una IPv4 válida (IP de Tailscale de la torre)."
is_ipv4 "$REMOTE_IP" || die "'$REMOTE_IP' no es una IPv4 válida (IP de Tailscale del portátil)."
[[ $LOCAL_IP != "$REMOTE_IP" ]] || die "la IP de la torre y la del portátil son la misma."

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

ensure_bridge() {
    if ! ip link show "$BRIDGE" &>/dev/null; then
        log "Creando $BRIDGE"
        ip link add "$BRIDGE" type bridge mcast_snooping 0
    fi
    # Por si el bridge ya existía con snooping activado
    ip link set "$BRIDGE" type bridge mcast_snooping 0
    ip link set "$BRIDGE" mtu "$MTU"
    ip addr replace "$OVERLAY_IP" dev "$BRIDGE"
    ip link set "$BRIDGE" up
}

# Los parámetros de VXLAN no se pueden cambiar en caliente: se recrea siempre.
ensure_vxlan() {
    if ip link show "$VXLAN_IFACE" &>/dev/null; then
        ip link del "$VXLAN_IFACE"
    fi
    log "Creando $VXLAN_IFACE (VNI $VNI, $LOCAL_IP -> $REMOTE_IP por $TUNNEL_IFACE)"
    ip link add "$VXLAN_IFACE" type vxlan id "$VNI" dstport "$VXLAN_PORT" \
        local "$LOCAL_IP" remote "$REMOTE_IP" dev "$TUNNEL_IFACE"
    ip link set "$VXLAN_IFACE" mtu "$MTU"
    ip link set "$VXLAN_IFACE" master "$BRIDGE"
    ip link set "$VXLAN_IFACE" up
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
    fi
}

do_up() {
    ensure_tunnel
    ensure_bridge
    ensure_vxlan
    check_firewall
    log "Overlay listo: $BRIDGE ${OVERLAY_IP%/*}, MTU $MTU"
}

# Nunca toca Tailscale: tirarlo dejaría la torre sin acceso remoto.
do_down() {
    if ip link show "$VXLAN_IFACE" &>/dev/null; then
        log "Borrando $VXLAN_IFACE"
        ip link del "$VXLAN_IFACE"
    fi
    if ip link show "$BRIDGE" &>/dev/null; then
        log "Borrando $BRIDGE"
        ip link del "$BRIDGE"
    fi
}

do_status() {
    echo "--- Tailscale"
    tailscale status || true
    tailscale ping -c 1 "$REMOTE_IP" || true
    echo "--- $VXLAN_IFACE"
    ip -d link show "$VXLAN_IFACE" || true
    echo "--- $BRIDGE"
    ip -d link show "$BRIDGE" || true
    ip -4 addr show dev "$BRIDGE" 2>/dev/null || true
    echo "multicast_snooping: $(cat "/sys/class/net/$BRIDGE/bridge/multicast_snooping" 2>/dev/null || echo '?')"
    bridge link show || true
    echo "--- Vecinos del overlay"
    ip neigh show dev "$BRIDGE" 2>/dev/null || true
    echo "--- Ping de paquete lleno al portátil por el overlay"
    ping -c 2 -W 2 -M do -s $(( MTU - 28 )) "$PEER_OVERLAY_IP" || true
}

case "$ACTION" in
    up)     require_root; do_up ;;
    down)   require_root; do_down ;;
    status) do_status ;;
    *)      echo "Error: acción desconocida '$ACTION'." >&2; usage ;;
esac
