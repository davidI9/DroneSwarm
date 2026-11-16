"""Conversión de landmarks normalizados de MediaPipe a metros en el frame `map`.

MediaPipe da cada landmark como (x, y, z): x e y normalizadas a [0, 1] sobre
la imagen (y crece hacia abajo) y z como profundidad relativa en la misma
escala que x. En el mundo, la mano se dibuja sobre el plano YZ:

- x de la imagen → eje Y del mundo;
- y de la imagen → −Z del mundo (bajar en la imagen es bajar en el mundo);
- profundidad → eje X del mundo.

El centro de la imagen cae sobre el eje Z, a la altura `height`.
"""

from collections.abc import Iterable, Sequence

Point = tuple[float, float, float]


def landmarks_to_world(
    normalized: Iterable[Sequence[float]], *, scale: float, height: float
) -> list[Point]:
    """Convierte landmarks normalizados a metros.

    `scale` es el tamaño en metros que ocupa todo el ancho de la imagen;
    `height` es la altura en metros del centro de la imagen.
    """
    return [
        (z * scale, (x - 0.5) * scale, height - (y - 0.5) * scale)
        for x, y, z in normalized
    ]
