"""
Capa de hardware SIMULADA.
Cuando llegue la Raspberry Pi 5, se reemplaza el contenido de estas
funciones por llamadas I2C reales.
"""

import random

_conteo = {
    "rojo": 0,
    "verde": 0,
    "azul": 0
}

_moviendo = False


def enviar_inicio(config, direccion):
    global _moviendo
    _moviendo = True
    print(f"[SIM] INICIO config={config} bifurcacion={direccion}")


def enviar_fin():
    global _moviendo
    _moviendo = False
    print("[SIM] FIN")


def leer_conteo_colores():
    if _moviendo and random.random() < 0.35:
        color = random.choice(list(_conteo))
        _conteo[color] += 1

    return dict(_conteo)


def reiniciar_conteo():
    for color in _conteo:
        _conteo[color] = 0