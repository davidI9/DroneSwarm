# Fundamentos teóricos

Este documento recoge la base teórica del proyecto: la red (ROS 2 sobre Zenoh, con un router en cada extremo) y el control de cobertura (descenso de gradiente sobre una teselación de Voronoi centroidal). Las decisiones concretas de implementación están en `CLAUDE.md`, en `docs/adr/` y en la issue #1.

## 1. Red y middleware: Zenoh con routers sobre Tailscale

### El problema: descubrimiento en una WAN de capa 3

El cliente (portátil) y el servidor (torre) no están en la misma LAN: el portátil se conecta desde cualquier red y llega a la torre por internet, a través de Tailscale. Tailscale es una VPN de capa 3: transporta paquetes IP unicast entre las máquinas del tailnet, pero no tramas Ethernet ni multicast.

El RMW por defecto de ROS 2 (DDS) descubre a los demás participantes enviando anuncios a una dirección UDP multicast. Ese multicast no atraviesa una red de capa 3, así que, sin más, los procesos del cliente y los del servidor nunca se verían.

### Zenoh en ROS 2 (`rmw_zenoh_cpp`)

`rmw_zenoh_cpp` es una implementación RMW de ROS 2 sobre el protocolo Zenoh. Los nodos, los topics, los mensajes y las QoS no cambian: es el mismo contrato de topics, con otro transporte debajo. Sus piezas son:

- **Sesión**: cada proceso ROS abre una sesión de Zenoh, por la que publica y se suscribe. Una sesión tiene un modo:
  - **peer**: la sesión se conecta al router local para descubrir a los demás y, una vez descubiertos, puede hablar directamente con otras sesiones peer de su misma máquina.
  - **client**: la sesión solo habla con un router. Todo su tráfico, incluido el que va a otras sesiones de la misma máquina, pasa por él.
- **Router** (`rmw_zenohd`, `pixi run router`): proceso que enlaza las sesiones de su máquina (y, en el servidor, las de sus VMs) con las del otro extremo. Mantiene la tabla de quién publica y quién se suscribe a cada topic y reenvía los datos a quien los necesita.
- **Enlace entre routers**: un router puede conectarse a otro por un endpoint explícito, como `tcp/<ip>:7447`. Los dos routers se intercambian lo que saben de sus sesiones, y los datos de un extremo llegan al otro por ese único enlace.

El descubrimiento ya no depende del multicast: cada sesión conoce de antemano el endpoint de su router, y cada router conoce el del otro extremo. Todo viaja por conexiones TCP unicast, que es justo lo que Tailscale transporta.

### Topología del proyecto

```
  Cliente (portátil)                      Servidor (torre, host)
 ┌─────────────────────┐                 ┌──────────────────────────┐
 │ mano falsa ┐        │                 │                          │
 │ vista      ├─ peer ─┤                 │      router del host     │
 │ RViz2      ┘        │   Tailscale     │   (escucha en TCP 7447)  │
 │   router del cliente├──── TCP 7447 ──►│                          │
 └─────────────────────┘                 └──────▲────────────▲──────┘
                                                │ client     │ client
                                    red interna │            │
                                        de VMs  │            │
                                         ┌──────┴──┐    ┌────┴────┐
                                         │  VM 1   │    │  VM 2   │
                                         │ drones  │    │ drones  │
                                         └─────────┘    └─────────┘
```

- Hay un router en el cliente y otro en el host del servidor. Las VMs no tienen router ni Tailscale.
- El router del servidor solo escucha, en TCP 7447 en todas las interfaces. El del cliente se conecta a la IP de Tailscale de la torre. Así el servidor no necesita la IP del portátil, que cambia con la red, ni el portátil necesita aceptar conexiones entrantes detrás de un NAT.
- Solo hay un enlace entre los dos extremos, y va sobre TCP sin cifrado propio, porque el cifrado ya lo pone Tailscale.
- En el cliente (y en el host, si ejecuta nodos), las sesiones van en modo peer contra el router local.
- En las VMs, las sesiones de los drones van en modo client contra el router del host, a través de la red interna del hipervisor. En modo client un dron nunca intenta conexiones directas con el portátil, que fallarían, porque no es alcanzable desde las VMs.

El papel de cada máquina y las IPs se escriben en una `.env` local, y `scripts/zenoh_config.py` construye con ella la configuración del router y de las sesiones (ver el `README`).

### Por qué no hay un punto único de fallo nuevo

En una red DDS sin multicast, la alternativa habitual es un servidor de descubrimiento central, que se descarta porque convierte ese servidor en un punto único de fallo para todo el sistema. Ese argumento no aplica aquí: no hay un router central, sino uno por extremo.

- **Si cae una VM**, sus drones dejan de publicar estado y el resto del enjambre los trata como drones caídos (`neighbor_timeout`) y absorbe su parte del dominio. Es la misma tolerancia a fallos de siempre, en la aplicación.
- **Si cae el router del cliente**, el cliente se queda sin ver el enjambre, pero los drones siguen viéndose entre ellos a través del router del host y mantienen la última mano (invariante 9).
- **Si cae el proceso del router del host**, todo el servidor queda aislado. Como las sesiones de las VMs van en modo client, todo su tráfico pasaba por ese router, incluido el de dos drones de la misma VM: ningún dron recibe ya el estado de ningún vecino. Cada uno los da por drones caídos, cree estar solo y se dirige al centroide de toda la mano con la última que conoce; la capa de seguridad no puede separar drones de los que no sabe nada. En el cliente, la vista del enjambre se queda vacía.

El router del host es, por tanto, el único punto de fallo de la topología. Se acepta porque no añade un riesgo nuevo: el host ya es un punto de fallo compartido por todas sus VMs, y si cae el host caen también ellas.

## 2. Descenso de gradiente en el espacio de posiciones

Para llevar un agente desde su posición $P \in \mathbb{R}^d$ hacia un objetivo $T \in \mathbb{R}^d$ se define el coste cuadrático:

$$E(P) = \tfrac{1}{2} \lVert P - T \rVert^2, \qquad \nabla E(P) = P - T$$

El agente avanza en sentido contrario al gradiente con una tasa $\alpha > 0$:

$$P_{k+1} = P_k - \alpha (P_k - T)$$

| Rango de $\alpha$ | Régimen |
|---|---|
| $0 < \alpha \le 1$ | Convergencia monótona, sin sobrepaso ($\alpha = 1$ llega en un paso) |
| $1 < \alpha < 2$ | Convergencia oscilatoria amortiguada |
| $\alpha \ge 2$ | Divergencia |

Criterio de parada: $\lVert \nabla E(P_k) \rVert \le \epsilon$.

## 3. Por qué fallan las asignaciones discretas cuando $M = N \pm c$

Con $M$ drones y $N$ landmarks, donde $M = N \pm c$ ($c \in \mathbb{Z}$):

| Enfoque | Formulación | Déficit ($M = N - c$) | Superávit ($M = N + c$) |
|---|---|---|---|
| Suma directa de errores | $E(P_i) = \tfrac{1}{2}\sum_{j=1}^{N} \lVert P_i - T_j \rVert^2$ | Todos colapsan al centroide global | Colapso al centroide global |
| Algoritmo húngaro (1:1) | Emparejamiento óptimo de $\min(M, N)$ | $c$ landmarks sin dron, huecos en la figura | $c$ drones excluidos fuera de la figura |
| Distancia de Chamfer | $\sum_i \min_j \lVert D_i - T_j \rVert^2 + \sum_j \min_i \lVert D_i - T_j \rVert^2$ | Drones atascados en puntos intermedios | Drones sobrantes atraídos a puntos ya ocupados (colisión) |

## 4. Control de cobertura: teselación de Voronoi centroidal (CVT)

En lugar de asignar drones a puntos, se cubre un dominio continuo $\Omega \subset \mathbb{R}^3$ (reconstruido a partir de los landmarks) con densidad $\rho(x) > 0$. Dadas las posiciones $\{D_1, \dots, D_M\}$, el dominio se divide en $M$ celdas:

$$V_i = \{ x \in \Omega \mid \lVert x - D_i \rVert \le \lVert x - D_j \rVert,\ \forall j \ne i \}$$

La calidad de la cobertura se mide con la energía de Lloyd:

$$E(D_1, \dots, D_M) = \sum_{i=1}^{M} \int_{V_i} \lVert x - D_i \rVert^2 \rho(x)\, dx$$

Al derivar respecto a $D_i$, los términos de frontera entre celdas vecinas se cancelan y queda:

$$\nabla_{D_i} E = 2 m_i (D_i - C_i), \qquad m_i = \int_{V_i} \rho(x)\, dx, \qquad C_i = \frac{1}{m_i} \int_{V_i} x\, \rho(x)\, dx$$

Aplicando descenso de gradiente, cada dron se mueve hacia el centroide de su propia celda:

$$D_i \leftarrow D_i - \alpha (D_i - C_i)$$

### Propiedades

- **Elasticidad ante $M = N \pm c$**: si faltan drones, las celdas crecen y cubren Ω sin huecos; si sobran, se subdividen y aumenta la resolución. Ningún dron queda fuera.
- **Separación**: toda celda de Voronoi de $\mathbb{R}^3$ es convexa (intersección de semiespacios), así que el centroide de la parte de Ω que le toca cae dentro de esa celda, y el camino recto hacia él no cruza la celda de un vecino. Este argumento supone posiciones distintas y exactas. El proyecto lo refuerza para drones solapados y vecinos estimados con retraso (ver *capa de seguridad* en `CLAUDE.md`).
- **Discretización**: Ω se representa con $K \gg M$ muestras. Cada muestra se asigna al dron más cercano, y $C_i$ es la media de las muestras asignadas.

### Ejecución distribuida y asíncrona

- Cada dron evalúa solo su propia celda, a partir de las posiciones que conoce de sus vecinos. No hay visión global.
- Aunque los temporizadores de los drones no estén sincronizados, $E$ actúa como función de Lyapunov y decrece hacia el equilibrio CVT para valores moderados de $\alpha$.
- **Dead reckoning**: la posición de cada vecino se proyecta al instante actual con su última velocidad conocida, $\hat{D}_j(t) = D_j + v_j \cdot (t - t_{\text{recv}})$, donde $t_{\text{recv}}$ es el instante local de recepción. Así no hace falta que los relojes entre máquinas estén sincronizados; el `header.stamp` del emisor solo sirve para visualización y depuración.
- **Dron caído**: un vecino del que no llega estado en `neighbor_timeout` (0.5 s por defecto) deja de contar en el siguiente tick, y los demás absorben su parte de Ω. Se detecta en la aplicación, con el mismo reloj local, y no con las QoS DEADLINE/LIVELINESS.

### Capa de seguridad

Tras calcular el paso $s$ hacia el centroide (o hacia la muestra más cercana si la celda está vacía) y limitarlo a $v_{\max}\,\Delta t$, el dron $i$ lo confina a su celda de Voronoi del espacio completo, encogida $r_{\text{safe}}/2$ a cada lado de cada bisector. Para cada vecino $j$, con $d_{ij} = \lVert D_j - D_i \rVert$ y $n_{ij} = (D_j - D_i)/d_{ij}$:

$$s \cdot n_{ij} \le \max\!\left(0,\ \frac{d_{ij} - r_{\text{safe}}}{2}\right)$$

- **Separación mínima**: si $i$ y $j$ aplican la regla con las mismas posiciones, $(D'_j - D'_i) \cdot n_{ij} \ge r_{\text{safe}}$, así que los dos drones nunca bajan de $r_{\text{safe}}$.
- **Invasión**: si ya están a menos de $r_{\text{safe}}$, el límite es 0 y solo se permiten componentes que alejan; la distancia entre ellos nunca disminuye.
- **Solapamiento exacto**: si $d_{ij} = 0$, $n_{ij}$ se toma como $g(j) - g(i)$ normalizado, donde $g$ es la dirección de la espiral áurea del id. Es determinista y opuesta para cada uno de los dos drones.
- **Vecinos con dead reckoning**: cada vecino aporta dos planos, el de su última posición recibida $D_j$ y el de su posición proyectada $\hat{D}_j(t)$. Un dron simulado solo se mueve en sus ticks, así que $D_j$ es su posición real hasta que vuelva a publicar, y con ella los dos drones aplican la regla sobre las mismas posiciones: es el plano que da la garantía. Usar solo $\hat{D}_j$ rompería la simetría, porque cada dron razonaría con una posición que el otro todavía no ocupa. El plano proyectado solo encoge más la celda y protege si el vecino se mueve como se predice. El reparto de muestras (la celda) usa $\hat{D}_j$.
- **Recorte**: el paso primero desliza sobre los planos que cruza (unas pocas pasadas de proyección) y después se escala en su propia dirección hasta cumplirlos todos. El paso nulo siempre es válido, así que el escalado existe. Ninguna de las dos operaciones alarga el paso.

**Ruptura de simetría**: al arrancar, el spawn se desplaza 5 cm en la dirección $g(\text{drone\_id})$ de la espiral áurea sobre la esfera, con ángulo $2\pi\,\text{id}/\varphi^2$ (el ángulo áureo) y altura $z = 1 - 2\,\{(\text{id} + \tfrac12)\sqrt2\}$, una secuencia independiente del ángulo para que las direcciones cubran toda la esfera. Como $\sqrt2$ es irracional, dos ids distintos nunca comparten dirección.

Con $r_{\text{safe}} = 0.3$ m caben pocos drones sobre una mano pequeña: en la mano falsa por defecto (4 m) el enjambre converge a separaciones de unos 0.15 m con 40 drones. Para muchos drones conviene agrandar la mano (parámetro `scale` de la mano falsa). Los tests usan una mano de 10 m.
