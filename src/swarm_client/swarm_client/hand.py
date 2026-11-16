"""Topología de la mano de MediaPipe y poses de referencia para la mano falsa."""

# Pares de landmarks unidos por un hueso, igual que HAND_CONNECTIONS de MediaPipe.
HAND_CONNECTIONS: tuple[tuple[int, int], ...] = (
    (0, 1), (1, 2), (2, 3), (3, 4),          # pulgar
    (0, 5), (5, 6), (6, 7), (7, 8),          # índice
    (5, 9), (9, 10), (10, 11), (11, 12),     # corazón
    (9, 13), (13, 14), (14, 15), (15, 16),   # anular
    (13, 17), (0, 17), (17, 18), (18, 19), (19, 20),  # meñique y palma
)

# Mano abierta, palma hacia la cámara, en coordenadas normalizadas de MediaPipe.
OPEN_HAND: tuple[tuple[float, float, float], ...] = (
    (0.50, 0.85, 0.00),
    (0.42, 0.80, -0.02), (0.36, 0.72, -0.03), (0.32, 0.64, -0.04), (0.29, 0.57, -0.05),
    (0.44, 0.58, -0.01), (0.42, 0.46, -0.02), (0.41, 0.38, -0.03), (0.40, 0.31, -0.03),
    (0.50, 0.56, -0.01), (0.50, 0.43, -0.02), (0.50, 0.34, -0.03), (0.50, 0.26, -0.03),
    (0.56, 0.58, -0.01), (0.57, 0.46, -0.02), (0.58, 0.38, -0.03), (0.58, 0.31, -0.03),
    (0.61, 0.62, -0.01), (0.63, 0.53, -0.02), (0.64, 0.47, -0.03), (0.65, 0.41, -0.03),
)
