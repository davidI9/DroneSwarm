# DroneSwarm

Enjambre de drones simulados sobre ROS 2 / DDS que, sin coordinador central, replica en tiempo real la forma de una mano capturada por la cámara del cliente. Cada dron se mueve por descenso de gradiente sobre una teselación de Voronoi centroidal (CVT). Es un proyecto para la asignatura de servicios distribuidos.

- Vocabulario: `GLOSSARY.md`. Escribe código, issues y docs con esos términos.
- Teoría (red DDS/VXLAN, CVT, fórmulas): `docs/teoria.md`.
- Decisiones de arquitectura: `docs/adr/`.
- Spec de implementación: issue #1 en GitHub.

## Estado

Hechos el esqueleto de extremo a extremo (issue #2), la cobertura CVT (issue #3), la capa de seguridad con ruptura de simetría (issue #4) y la tolerancia a fallos (issue #5). Hay tres paquetes en `src/`:

- `swarm_interfaces`: mensajes `HandLandmarks` y `DroneState`.
- `swarm_drone`: `DroneController` y el nodo `drone_<id>`. El dron densifica el dominio (`src/domain.cpp`) y avanza hacia el centroide de su celda con el último estado conocido de cada vecino, a través de la capa de seguridad. Olvida a los vecinos que no publican en `neighbor_timeout`, proyecta su posición por dead reckoning y señala un `drone_id` duplicado, ante el que el nodo termina con error fatal. Los tests usan el bus falso `test/fake_bus.hpp` y la mano de referencia `test/test_hand.hpp` (10 m, para que quepan 40 drones separados `r_safe`).
- `swarm_client`: conversión de landmarks, mano falsa estática y la configuración de RViz2.

Lo siguiente son las issues #6 a #8 (mano animada, visión y despliegue).

## Despliegue

- **Cliente** (portátil): nodo de visión (OpenCV + MediaPipe) o mano falsa, y RViz2.
- **Servidor** (ordenador de torre): varias **VMs**, cada una con uno o varios drones. Cada dron es un proceso lanzado a mano en su propia terminal con `pixi run …` y su `drone_id` como parámetro. Sin contenedores ni orquestador.
- La red de capa 2 (VXLAN) que une cliente y VMs la monta y la mantiene el usuario como parte de la asignatura. El código solo aporta un perfil de Fast DDS de ejemplo, opcional, para fijar la interfaz `vxlan0`.
- Middleware: Fast DDS (RMW por defecto de ROS 2).

## Contrato DDS

| Topic | Contenido | QoS |
|---|---|---|
| `/hand/landmarks` | 21 landmarks en metros, frame `map` | RELIABLE, TRANSIENT_LOCAL, KEEP_LAST(1) |
| `/hand/markers` | Mano objetivo para RViz2 | por defecto |
| `/swarm/state` | `drone_id`, uuid de instancia, posición, velocidad, timestamp | BEST_EFFORT, KEEP_LAST |
| `/swarm/markers` | Esfera de cada dron | por defecto |

Por la red solo viajan landmarks; cada dron reconstruye el dominio (ADR 0001). La conversión de coordenadas de MediaPipe a metros se hace en el cliente.

## Invariantes del dron

Estas reglas garantizan que dos drones nunca vayan a por el mismo espacio. Cualquier cambio en el dron debe conservarlas todas:

1. **Densificación determinista**: con los mismos landmarks, todos los drones obtienen exactamente el mismo dominio (huesos + palma).
2. **Desempate por id**: una muestra a la misma distancia de dos drones pertenece al de `drone_id` menor.
3. **Ruptura de simetría**: al arrancar, el spawn se desplaza unos centímetros en una dirección determinista derivada del `drone_id`.
4. **Capa de seguridad**: cada paso queda dentro de la propia celda de Voronoi del espacio completo, encogida `r_safe / 2` a cada lado de cada bisector, de modo que dos drones no bajan de `r_safe`. Cada vecino aporta el bisector de su última posición recibida y el de su posición proyectada. Un dron que ya está demasiado cerca de un vecino solo puede alejarse de él.
5. **Celda vacía**: el dron se dirige hacia la muestra más cercana, siempre a través de la capa de seguridad.
6. **Dron caído**: un vecino que no envía estado en `neighbor_timeout` deja de contar. Se detecta con un timeout en la aplicación, no con las QoS DEADLINE/LIVELINESS.
7. **Dead reckoning con tiempo de recepción local**: los relojes entre máquinas no están sincronizados.
8. **`drone_id` duplicado**: si un dron recibe estado con su mismo id y distinto uuid de instancia, termina con un error fatal.
9. **Sin mano**: el dron se queda quieto en su spawn hasta que llega la primera mano. Si la mano deja de llegar, mantiene la última.

El nodo ROS se llama `drone_<id>` para que varios drones convivan en una VM.

## Estructura y tests

- Un paquete de interfaces, otro de C++ con el dron y otro de Python con el cliente.
- Toda la lógica del dron vive en `DroneController`, una clase C++ sin dependencias de ROS. El nodo ROS es una capa fina que traduce mensajes y temporizadores.
- Los tests principales (gtest) ejercitan `DroneController`: varios drones en el mismo proceso, unidos por un bus falso, y se comprueba solo comportamiento observable. La conversión de landmarks del cliente se prueba con pytest.
- Se compila con `pixi run build`. Las demás tareas están en `pixi.toml`.

## Convenciones

- Todo lo que se publique (commits, PRs, issues y documentos) va firmado únicamente por el usuario, sin trailers `Co-Authored-By` ni pies de atribución.
- La documentación del proyecto se escribe en español.

## Agent skills

### Issue tracker

Las issues y specs viven en GitHub Issues de `davidI9/DroneSwarm` (CLI `gh`). See `docs/agents/issue-tracker.md`.

### Triage labels

Vocabulario por defecto: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Un único contexto: `GLOSSARY.md` y `docs/adr/` en la raíz. See `docs/agents/domain.md`.
