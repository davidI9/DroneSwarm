# Por la red viajan los landmarks; cada dron reconstruye el dominio

El cliente publica únicamente los 21 landmarks de la mano, ya en metros y en el frame del mundo, y cada dron densifica por su cuenta el dominio Ω con un algoritmo determinista. El diseño original publicaba desde el cliente la nube densa de K muestras. Lo cambiamos por tres motivos: el mensaje pasa de unos K·24 B (≈36 KB con K≈1500) a unos 500 B por fotograma; el contrato del topic se reduce a "las coordenadas de la mano"; y la forma del dominio se puede cambiar sin tocar el cliente.

## Consecuencias

- La densificación tiene que ser **idéntica y determinista** en todos los drones: mismo orden, sin aleatoriedad y la misma versión del código desplegada en todas las VMs. Si dos drones reconstruyen Ω distintos, sus celdas dejan de encajar y el reparto del espacio deja de estar garantizado.
- Cada dron gasta CPU en densificar con cada mano nueva. Es un coste despreciable para K del orden de miles.

## Opciones consideradas

- **El cliente publica la nube densa** (`/hand/domain_cloud`): Ω es única por construcción, pero el tráfico se multiplica por ~70 en cada fotograma, y llega a todos los drones de todas las VMs.
