from flask import Flask, render_template
from flask_socketio import SocketIO, emit

import interfaz_hardware as hw

app = Flask(__name__)
socketio = SocketIO(
    app,
    async_mode="threading",
    cors_allowed_origins="*"
)

ACCIONES = {"frenar", "retroceder", "velocidad"}

estado = {
    "moviendo": False,
    "config": {
        "rojo": "frenar",
        "verde": "retroceder",
        "azul": "velocidad"
    },
    "direccion": "izquierda",
}


def snapshot():
    return {
        **estado,
        "conteo": hw.leer_conteo_colores()
    }


@app.route("/")
def index():
    return render_template("index.html")


@socketio.on("connect")
def on_connect():
    emit("estado", snapshot())


@socketio.on("inicio")
def on_inicio():
    if not estado["moviendo"]:
        hw.reiniciar_conteo()

        hw.enviar_inicio(
            estado["config"],
            estado["direccion"]
        )

        estado["moviendo"] = True

    socketio.emit("estado", snapshot())


@socketio.on("fin")
def on_fin():
    hw.enviar_fin()

    estado["moviendo"] = False

    socketio.emit("estado", snapshot())


@socketio.on("configurar")
def on_configurar(data):

    if estado["moviendo"]:
        emit(
            "error_msg",
            "No se puede cambiar la configuración mientras "
            "el carro está en movimiento."
        )

        emit("estado", snapshot())
        return

    for color in ("rojo", "verde", "azul"):
        if data.get(color) in ACCIONES:
            estado["config"][color] = data[color]

    if data.get("direccion") in ("izquierda", "derecha"):
        estado["direccion"] = data["direccion"]

    socketio.emit("estado", snapshot())


def difundir_conteo():
    while True:
        socketio.sleep(0.5)

        socketio.emit(
            "conteo",
            hw.leer_conteo_colores()
        )


if __name__ == "__main__":

    socketio.start_background_task(
        difundir_conteo
    )

    socketio.run(
        app,
        host="0.0.0.0",
        port=5000,
        allow_unsafe_werkzeug=True
    )