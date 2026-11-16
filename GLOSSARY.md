# DroneSwarm

Simulación distribuida de un enjambre de drones que, sin coordinador central, se reparten el espacio para replicar en tiempo real la forma de una mano capturada por cámara.

## Lenguaje

### Despliegue

**Cliente**:
El portátil del usuario: captura la mano con la cámara y visualiza el enjambre.
_Evitar_: estación base, frontend

**Servidor**:
El ordenador de torre que aloja las VMs donde se ejecutan los drones.
_Evitar_: backend, clúster

**VM**:
Máquina virtual del servidor que ejecuta uno o varios drones, cada uno en su propio proceso.
_Evitar_: instancia, contenedor, pod

### Enjambre

**Dron**:
Agente autónomo simulado del enjambre; uno por proceso, identificado por su `drone_id`.
_Evitar_: nodo (ambiguo con nodo ROS), agente

**Enjambre**:
El conjunto de todos los drones vivos en un instante; su tamaño M varía en tiempo de ejecución.
_Evitar_: flota, swarm

**Spawn**:
La posición en la que aparece un dron al arrancar.
_Evitar_: posición inicial, origen

**Vecino**:
Cualquier otro dron vivo del que un dron conoce su estado reciente.
_Evitar_: peer, par

**Dron caído**:
Un dron del que el resto del enjambre lleva demasiado tiempo sin recibir estado y que, por tanto, deja de considerarse vecino.
_Evitar_: dron muerto, dron desconectado

### Cobertura

**Celda**:
La parte del dominio formada por las muestras que están más cerca de un dron que de cualquier vecino.
_Evitar_: región, zona, celda de Voronoi (salvo al hablar del espacio completo)

**Centroide**:
El punto medio de la celda de un dron; es hacia donde ese dron se dirige.
_Evitar_: objetivo, target, centro

**Capa de seguridad**:
La restricción que impide que un dron, en cualquier paso, entre en el espacio que corresponde a un vecino, aunque ambos hayan nacido solapados.
_Evitar_: anticolisión, filtro de colisiones

### Mano

**Landmarks**:
Los N puntos articulares de la mano extraídos por visión; es lo único que el cliente envía a los drones.
_Evitar_: vértices, keypoints, articulaciones

**Dominio (Ω)**:
La nube densa de muestras que representa la forma de la mano y que el enjambre debe cubrir; cada dron la reconstruye a partir de los landmarks.
_Evitar_: nube de la mano, domain cloud

**Muestra**:
Cada uno de los K puntos que componen el dominio.
_Evitar_: sample point, píxel
