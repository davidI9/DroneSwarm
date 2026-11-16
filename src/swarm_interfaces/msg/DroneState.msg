# Estado que cada dron publica en /swarm/state.
# header.stamp es el reloj del emisor: solo para visualización y depuración.
std_msgs/Header header
uint32 drone_id
# Identificador aleatorio generado al arrancar el proceso; distingue dos drones lanzados con el mismo drone_id.
uint8[16] instance_uuid
geometry_msgs/Point position
geometry_msgs/Vector3 velocity
