#!/usr/bin/env python3
# =====================================================================
# Interfaz del carro - Grupo 6 (MT-7003) - corre en la Raspberry Pi 5
# Maestro I2C (bus 1: pin 3 SDA, pin 5 SCL) -> ESP32-S3 esclavo en 0x42
# =====================================================================
import tkinter as tk
from tkinter import ttk, messagebox
from smbus2 import SMBus, i2c_msg

BUS_I2C = 1
DIR_ESP = 0x42
PERIODO_MS = 100          # lectura del estado cada 100 ms (tiempo real)

CMD_INICIO, CMD_FIN, CMD_CONFIG, CMD_RUTA, CMD_VEL, CMD_MOVER = 1, 2, 3, 4, 5, 6
DIR_ADELANTE, DIR_ATRAS, DIR_IZQ, DIR_DER, DIR_GIRO_IZQ, DIR_GIRO_DER = 0, 1, 2, 3, 4, 5
NOMBRES_DIR = ["Adelante", "Atrás", "Curva izquierda", "Curva derecha",
               "Giro en su lugar (izq.)", "Giro en su lugar (der.)"]

ACCIONES = ["Freno (10 s)", "Retroceso (3 s)", "Cambio de velocidad"]
ESTADOS = {0: "DETENIDO", 1: "AVANZANDO", 2: "FRENANDO", 3: "RETROCEDIENDO"}
ERRORES = {
    0: "",
    1: "Configuración rechazada: el carro está en movimiento",
    2: "Trama inválida (checksum)",
    3: "Comando inválido",
    4: "Choque detectado (switch CLP): carro detenido",
    5: "Se perdió la comunicación: carro detenido",
    6: "Presione INICIO antes de cambiar la dirección",
}


class EnlaceI2C:
    """Comunicación con el ESP32 según el protocolo del proyecto."""

    def __init__(self):
        self.bus = SMBus(BUS_I2C)

    def enviar(self, cmd, a1=0, a2=0, a3=0):
        trama = [cmd, a1, a2, a3, cmd ^ a1 ^ a2 ^ a3]
        self.bus.i2c_rdwr(i2c_msg.write(DIR_ESP, trama))

    def leer(self):
        msg = i2c_msg.read(DIR_ESP, 8)      # lectura simple, sin "registro"
        self.bus.i2c_rdwr(msg)
        d = list(msg)
        chk = 0
        for b in d[:7]:
            chk ^= b
        if chk != d[7]:
            return None
        return {"estado": d[0], "error": d[1], "r": d[2], "g": d[3], "b": d[4],
                "vbat": d[5] / 10.0, "vel_alta": d[6]}


class Interfaz:
    def __init__(self, root):
        self.root = root
        self.enlace = EnlaceI2C()
        self.estado = None                   # None = sin datos del ESP32

        root.title("Carro seguidor - Grupo 6")
        root.geometry("560x760")
        root.protocol("WM_DELETE_WINDOW", self.cerrar)
        fuente = ("Helvetica", 12)

        # --- Inicio / Fin ---
        marco = ttk.LabelFrame(root, text="Control")
        marco.pack(fill="x", padx=10, pady=6)
        tk.Button(marco, text="INICIO", bg="#2e7d32", fg="white", width=14, height=2,
                  font=("Helvetica", 14, "bold"), command=self.inicio).pack(side="left", padx=10, pady=8)
        tk.Button(marco, text="FIN", bg="#c62828", fg="white", width=14, height=2,
                  font=("Helvetica", 14, "bold"), command=self.fin).pack(side="left", padx=10, pady=8)

        # --- Dirección (modo manual para pruebas) ---
        marco = ttk.LabelFrame(root, text="Dirección manual (flechas del teclado o botones)")
        marco.pack(fill="x", padx=10, pady=6)
        pad = tk.Frame(marco)
        pad.pack(pady=6)
        estilo = {"width": 12, "height": 2, "font": ("Helvetica", 11, "bold")}
        tk.Button(pad, text="▲ Adelante", command=lambda: self.mover(DIR_ADELANTE), **estilo).grid(row=0, column=1, padx=3, pady=3)
        tk.Button(pad, text="◀ Izquierda", command=lambda: self.mover(DIR_IZQ), **estilo).grid(row=1, column=0, padx=3, pady=3)
        tk.Button(pad, text="■ FIN", bg="#c62828", fg="white", command=self.fin, **estilo).grid(row=1, column=1, padx=3, pady=3)
        tk.Button(pad, text="Derecha ▶", command=lambda: self.mover(DIR_DER), **estilo).grid(row=1, column=2, padx=3, pady=3)
        tk.Button(pad, text="▼ Atrás", command=lambda: self.mover(DIR_ATRAS), **estilo).grid(row=2, column=1, padx=3, pady=3)
        tk.Button(pad, text="⟲ Girar izq.", command=lambda: self.mover(DIR_GIRO_IZQ), **estilo).grid(row=2, column=0, padx=3, pady=3)
        tk.Button(pad, text="Girar der. ⟳", command=lambda: self.mover(DIR_GIRO_DER), **estilo).grid(row=2, column=2, padx=3, pady=3)
        self.lbl_dir = ttk.Label(marco, text="Dirección: Adelante", font=fuente)
        self.lbl_dir.pack(pady=2)

        # Atajos de teclado
        root.bind("<Up>",    lambda e: self.mover(DIR_ADELANTE))
        root.bind("<Down>",  lambda e: self.mover(DIR_ATRAS))
        root.bind("<Left>",  lambda e: self.mover(DIR_IZQ))
        root.bind("<Right>", lambda e: self.mover(DIR_DER))
        root.bind("a",       lambda e: self.mover(DIR_GIRO_IZQ))
        root.bind("d",       lambda e: self.mover(DIR_GIRO_DER))
        root.bind("<space>", lambda e: self.fin())
        root.bind("<Return>", lambda e: self.inicio())

        # --- Velocidad y ruta ---
        marco = ttk.LabelFrame(root, text="Velocidad y ruta en la bifurcación")
        marco.pack(fill="x", padx=10, pady=6)
        self.vel = tk.IntVar(value=0)
        ttk.Label(marco, text="Velocidad:", font=fuente).grid(row=0, column=0, sticky="w", padx=6, pady=4)
        ttk.Radiobutton(marco, text="Baja", variable=self.vel, value=0, command=self.cambiar_vel).grid(row=0, column=1)
        ttk.Radiobutton(marco, text="Alta", variable=self.vel, value=1, command=self.cambiar_vel).grid(row=0, column=2)
        self.ruta = tk.IntVar(value=0)
        ttk.Label(marco, text="Ruta en la Y:", font=fuente).grid(row=1, column=0, sticky="w", padx=6, pady=4)
        ttk.Radiobutton(marco, text="Izquierda", variable=self.ruta, value=0, command=self.cambiar_ruta).grid(row=1, column=1)
        ttk.Radiobutton(marco, text="Derecha", variable=self.ruta, value=1, command=self.cambiar_ruta).grid(row=1, column=2)

        # --- Asociación color - acción ---
        marco = ttk.LabelFrame(root, text="Asociación color → acción (solo con el carro detenido)")
        marco.pack(fill="x", padx=10, pady=6)
        self.combos = []
        for i, color in enumerate(["Rojo", "Verde", "Azul"]):
            ttk.Label(marco, text=color, font=fuente).grid(row=i, column=0, sticky="w", padx=6, pady=3)
            cb = ttk.Combobox(marco, values=ACCIONES, state="readonly", width=24)
            cb.current(i)
            cb.grid(row=i, column=1, padx=6, pady=3)
            self.combos.append(cb)
        ttk.Button(marco, text="Aplicar configuración", command=self.aplicar_config).grid(
            row=3, column=0, columnspan=2, pady=6)

        # --- Estado en tiempo real ---
        marco = ttk.LabelFrame(root, text="Estado en tiempo real")
        marco.pack(fill="x", padx=10, pady=6)
        self.lbl_con = ttk.Label(marco, text="Conexión: ---", font=fuente)
        self.lbl_con.grid(row=0, column=0, sticky="w", padx=6, pady=2)
        self.lbl_est = ttk.Label(marco, text="Estado: ---", font=fuente)
        self.lbl_est.grid(row=1, column=0, sticky="w", padx=6, pady=2)
        self.lbl_bat = ttk.Label(marco, text="Batería: --- V", font=fuente)
        self.lbl_bat.grid(row=2, column=0, sticky="w", padx=6, pady=2)
        self.lbl_cnt = ttk.Label(marco, text="Rojo: 0   Verde: 0   Azul: 0", font=("Helvetica", 13, "bold"))
        self.lbl_cnt.grid(row=3, column=0, sticky="w", padx=6, pady=6)
        self.lbl_err = tk.Label(root, text="", fg="#c62828", font=fuente, wraplength=520)
        self.lbl_err.pack(pady=4)

        self.sondear()

    # ---------------- Envío de comandos ----------------
    def _enviar(self, *args):
        try:
            self.enlace.enviar(*args)
            return True
        except OSError:
            messagebox.showerror("Error de comunicación", "No se pudo comunicar con el ESP32 por I2C.")
            return False

    def inicio(self):
        if self._enviar(CMD_INICIO):
            self.lbl_dir.config(text=f"Dirección: {NOMBRES_DIR[DIR_ADELANTE]}")

    def mover(self, direccion):
        if self.estado in (None, 0):
            self.lbl_err.config(text=ERRORES[6])
            return
        if self._enviar(CMD_MOVER, direccion):
            self.lbl_dir.config(text=f"Dirección: {NOMBRES_DIR[direccion]}")

    def fin(self):
        self._enviar(CMD_FIN)

    def cambiar_vel(self):
        self._enviar(CMD_VEL, self.vel.get())

    def cambiar_ruta(self):
        self._enviar(CMD_RUTA, self.ruta.get())

    def aplicar_config(self):
        if self.estado is None:
            messagebox.showerror("Error", "No hay comunicación con el ESP32.")
            return
        if self.estado != 0:                 # primera barrera: la interfaz
            messagebox.showerror("Error", "No se puede cambiar la asociación color-acción "
                                          "mientras el carro está en movimiento.\nPresione FIN primero.")
            return
        acc = [cb.current() for cb in self.combos]
        if len(set(acc)) != 3:
            messagebox.showerror("Error", "Cada color debe tener una acción distinta.")
            return
        if self._enviar(CMD_CONFIG, *acc):
            messagebox.showinfo("Configuración", "Asociación color-acción aplicada.")

    # ---------------- Lectura periódica del estado ----------------
    def sondear(self):
        try:
            d = self.enlace.leer()
            if d is None:
                self.lbl_con.config(text="Conexión: trama inválida", foreground="#e65100")
            else:
                self.estado = d["estado"]
                self.lbl_con.config(text="Conexión: OK", foreground="#2e7d32")
                self.lbl_est.config(text=f"Estado: {ESTADOS.get(d['estado'], '?')}"
                                         f"   ({'alta' if d['vel_alta'] else 'baja'} velocidad)")
                color_bat = "#c62828" if d["vbat"] < 9.6 else "black"
                self.lbl_bat.config(text=f"Batería: {d['vbat']:.1f} V", foreground=color_bat)
                self.lbl_cnt.config(text=f"Rojo: {d['r']}   Verde: {d['g']}   Azul: {d['b']}")
                self.lbl_err.config(text=ERRORES.get(d["error"], f"Error {d['error']}"))
        except OSError:
            self.estado = None
            self.lbl_con.config(text="Conexión: SIN CONEXIÓN con el ESP32", foreground="#c62828")
        self.root.after(PERIODO_MS, self.sondear)

    def cerrar(self):
        try:
            self.enlace.enviar(CMD_FIN)      # por seguridad, detiene el carro al cerrar
        except OSError:
            pass
        self.root.destroy()


if __name__ == "__main__":
    raiz = tk.Tk()
    Interfaz(raiz)
    raiz.mainloop()
