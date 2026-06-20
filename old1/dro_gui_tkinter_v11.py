import threading
import time
import tkinter as tk

try:
    import serial
except Exception:
    serial = None

from manuale_v4 import ManualeFrame
from spianatura_xy_v5 import SpianaturaXYFrame


class DROApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("DRO Fresa")
        self.configure(bg="#101010")
        self.attributes("-fullscreen", True)

        self.bind("<Escape>", self.exit_fullscreen)
        self.bind("<F11>", self.enter_fullscreen)

        self.positions = {"X": "000,000", "Y": "000,000", "Z": "000,000"}

        self.serial_port = "/dev/serial0"
        self.baudrate = 115200
        self.serial_conn = None
        self.serial_lock = threading.Lock()

        self.last_manual_cmd = {"x": 0.0, "y": 0.0, "z": 0.0}
        self.last_manual_send_time = 0.0
        self.manual_keepalive_interval_s = 0.20

        self.container = tk.Frame(self, bg="#101010")
        self.container.pack(fill="both", expand=True)
        self.current_frame = None

        self.start_serial_reader()
        self.show_main_menu()
        self.after(50, self.manual_keepalive_task)

    def exit_fullscreen(self, event=None):
        self.attributes("-fullscreen", False)

    def enter_fullscreen(self, event=None):
        self.attributes("-fullscreen", True)

    def get_axis_values(self):
        return dict(self.positions)

    def send_manual_command(self, data):
        cmd = {
            "x": float(data.get("x", 0.0)),
            "y": float(data.get("y", 0.0)),
            "z": float(data.get("z", 0.0)),
        }
        self.last_manual_cmd = cmd
        self.write_jog_command(cmd)
        self.last_manual_send_time = time.time()

    def manual_keepalive_task(self):
        now = time.time()
        if (now - self.last_manual_send_time) >= self.manual_keepalive_interval_s:
            self.write_jog_command(self.last_manual_cmd)
            self.last_manual_send_time = now
        self.after(50, self.manual_keepalive_task)

    def write_jog_command(self, cmd):
        if self.serial_conn is None:
            return
        line = f'JOG,X:{cmd["x"]:.1f},Y:{cmd["y"]:.1f},Z:{cmd["z"]:.1f}\n'
        try:
            with self.serial_lock:
                self.serial_conn.write(line.encode("utf-8"))
                self.serial_conn.flush()
        except Exception:
            pass

    def clear_current_frame(self):
        if self.current_frame is not None:
            self.current_frame.destroy()
            self.current_frame = None

    def show_main_menu(self):
        self.clear_current_frame()
        frame = tk.Frame(self.container, bg="#101010")
        frame.pack(fill="both", expand=True)
        self.current_frame = frame

        frame.grid_rowconfigure(0, weight=0)
        frame.grid_rowconfigure(1, weight=1)
        frame.grid_columnconfigure(0, weight=1)

        top_frame = tk.Frame(frame, bg="#101010", padx=20, pady=18)
        top_frame.grid(row=0, column=0, sticky="ew")
        for col in range(3):
            top_frame.grid_columnconfigure(col, weight=1)

        self.menu_value_labels = {}
        for idx, axis in enumerate(("X", "Y", "Z")):
            panel = tk.Frame(top_frame, bg="#1a1a1a", highlightthickness=2,
                             highlightbackground="#2f2f2f", padx=18, pady=10)
            panel.grid(row=0, column=idx, padx=10, sticky="nsew")

            tk.Label(panel, text=axis, font=("Arial", 20, "bold"),
                     fg="#cfcfcf", bg="#1a1a1a").pack(anchor="center")

            value_label = tk.Label(panel, text=self.positions[axis], font=("Courier New", 34, "bold"),
                                   fg="#ffffff", bg="#1a1a1a")
            value_label.pack(anchor="center", pady=(6, 0))
            self.menu_value_labels[axis] = value_label

        tk.Label(frame, text="DRO FRESA", font=("Arial", 30, "bold"),
                 fg="#ffffff", bg="#101010").grid(row=1, column=0, sticky="n", pady=(20, 10))

        buttons_frame = tk.Frame(frame, bg="#101010")
        buttons_frame.grid(row=1, column=0)

        buttons = [
            ("manuale", self.show_manuale),
            ("spianatura XY", self.show_spianatura_xy),
            ("foro grande", None),
            ("spianatura XZ", None),
            ("spianatura YZ", None),
        ]

        for i, (text, command) in enumerate(buttons):
            btn = tk.Button(buttons_frame, text=text.upper(),
                            command=command if command is not None else (lambda t=text: self.show_placeholder(t)),
                            font=("Arial", 24, "bold"), bg="#2d7ef7", fg="white",
                            activebackground="#4d96ff", activeforeground="white",
                            relief="flat", bd=0, padx=30, pady=22, width=18)
            btn.grid(row=i, column=0, pady=10)

        tk.Button(frame, text="ESCI", command=self.destroy, font=("Arial", 18, "bold"),
                  bg="#aa2e25", fg="white", activebackground="#c23c32",
                  activeforeground="white", relief="flat", bd=0,
                  padx=18, pady=10).grid(row=2, column=0, sticky="se", padx=20, pady=20)

        self.refresh_menu_axes()

    def refresh_menu_axes(self):
        if hasattr(self, "menu_value_labels") and self.current_frame is not None:
            for axis in ("X", "Y", "Z"):
                if axis in self.menu_value_labels:
                    self.menu_value_labels[axis].configure(text=self.positions.get(axis, "000,000"))
            self.after(100, self.refresh_menu_axes)

    def show_manuale(self):
        self.clear_current_frame()
        self.current_frame = ManualeFrame(self.container, on_back=self.show_main_menu,
                                          get_axis_values=self.get_axis_values,
                                          on_manual_command=self.send_manual_command,
                                          bg="#101010")
        self.current_frame.pack(fill="both", expand=True)

    def show_spianatura_xy(self):
        self.clear_current_frame()
        self.current_frame = SpianaturaXYFrame(self.container, on_back=self.show_main_menu,
                                               get_axis_values=self.get_axis_values, bg="#101010")
        self.current_frame.pack(fill="both", expand=True)

    def show_placeholder(self, text):
        self.clear_current_frame()
        frame = tk.Frame(self.container, bg="#101010")
        frame.pack(fill="both", expand=True)
        self.current_frame = frame

        tk.Label(frame, text=f"{text.upper()}\n\nPagina non ancora sviluppata",
                 font=("Arial", 28, "bold"), fg="#ffffff", bg="#101010",
                 justify="center").pack(expand=True)

        tk.Button(frame, text="INDIETRO", command=self.show_main_menu, font=("Arial", 18, "bold"),
                  bg="#aa2e25", fg="white", activebackground="#c23c32",
                  activeforeground="white", relief="flat", bd=0,
                  padx=18, pady=10).pack(pady=20)

    def start_serial_reader(self):
        if serial is None:
            return
        try:
            self.serial_conn = serial.Serial(self.serial_port, self.baudrate, timeout=0.05)
            threading.Thread(target=self.serial_loop, daemon=True).start()
        except Exception:
            self.serial_conn = None

    def serial_loop(self):
        rx_buffer = ""
        while True:
            try:
                if self.serial_conn is None:
                    time.sleep(0.2)
                    continue
                with self.serial_lock:
                    chunk = self.serial_conn.read(self.serial_conn.in_waiting or 1).decode(errors="ignore")
                if not chunk:
                    time.sleep(0.01)
                    continue
                rx_buffer += chunk
                while "\n" in rx_buffer:
                    line, rx_buffer = rx_buffer.split("\n", 1)
                    line = line.strip()
                    if line:
                        self.handle_serial_line(line)
            except Exception:
                time.sleep(0.1)

    def handle_serial_line(self, line):
        if not (line.startswith("X:") and ",Y:" in line and ",Z:" in line):
            return
        values = {}
        for part in line.split(","):
            if ":" not in part:
                continue
            axis, raw = part.split(":", 1)
            axis = axis.strip().upper()
            raw = raw.strip()
            if axis in ("X", "Y", "Z"):
                values[axis] = self.format_value(raw)
        for axis in ("X", "Y", "Z"):
            if axis in values:
                self.positions[axis] = values[axis]

    def format_value(self, raw):
        raw = str(raw).strip().replace(".", ",")
        if raw == "":
            return "000,000"
        sign = ""
        if raw.startswith("-"):
            sign = "-"
            raw = raw[1:]
        if "," in raw:
            int_part, dec_part = raw.split(",", 1)
        else:
            int_part, dec_part = raw, ""
        int_part = "".join(ch for ch in int_part if ch.isdigit())
        dec_part = "".join(ch for ch in dec_part if ch.isdigit())
        if int_part == "":
            int_part = "0"
        int_part = int_part[-3:].zfill(3)
        dec_part = dec_part[:3].ljust(3, "0")
        return f"{sign}{int_part},{dec_part}"


if __name__ == "__main__":
    app = DROApp()
    app.mainloop()
