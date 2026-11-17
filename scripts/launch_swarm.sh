#!/usr/bin/env bash
# Lanza N drones (40 por defecto) en segundo plano, cada uno con su propio
# drone_id. Al salir del script (Ctrl+C, fin o error) se cierran todos.
#
#   scripts/launch_swarm.sh            # drones 0..39
#   scripts/launch_swarm.sh 10         # drones 0..9
#   scripts/launch_swarm.sh 10 100     # drones 100..109
set -u

N="${1:-40}"
FIRST_ID="${2:-0}"
LOG_DIR="${LOG_DIR:-log/drones}"
mkdir -p "$LOG_DIR"

cleanup() {
  # Que la señal enviada al grupo no mate a este script antes de terminar.
  trap '' INT TERM
  trap - EXIT
  echo
  echo "Cerrando drones..."
  # SIGINT a todo el grupo de procesos: rclcpp se apaga de forma limpia.
  kill -INT 0 2>/dev/null
  # Hasta 5 s de margen para que salgan solos.
  for _ in $(seq 50); do
    [[ -z "$(jobs -rp)" ]] && break
    sleep 0.1
  done
  # Los que sigan vivos se matan a la fuerza (esto incluye al script).
  if [[ -n "$(jobs -rp)" ]]; then
    echo "Forzando el cierre de los que no han respondido"
    kill -KILL 0 2>/dev/null
  fi
  echo "Hecho."
  exit 0
}
trap cleanup INT TERM EXIT

for ((i = 0; i < N; i++)); do
  id=$((FIRST_ID + i))
  pixi run drone "$id" >"$LOG_DIR/drone_$id.log" 2>&1 &
done

echo "Lanzados $N drones (ids $FIRST_ID..$((FIRST_ID + N - 1)))."
echo "Logs en $LOG_DIR/drone_<id>.log. Ctrl+C para cerrarlos todos."
wait
