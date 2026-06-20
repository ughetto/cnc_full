import serial
import threading
import time
import tkinter as tk
from tkinter import messagebox

from spianatura_xy_v4 import SpianaturaXYFrame

SERIAL_PORT = "/dev/ttyAMA0"
BAUDRATE = 115200
MM_PER_COUNT = 0.005
REFRESH_MS = 100

latest_counts = {"X": 0, "Y": 0, "Z": 0}
zero_offsets = {"X": 0, "Y": 0, "Z": 0}
serial_ok = False
lock = threading.Lock()


def parse_line(line: str):
    result = {}
    for part in line.split(","):
        if ":" not in part:
            continue
        key, value = part.split(":", 1)
        key = key.strip().upper()
        value = value.strip()
        if key in ("X", "Y", "Z"):
            try:
                result[key] = int(value)
            except ValueError:
                return None

    if all(axis in result for axis in ("X", "Y", "Z")):
        return result
    return None


def serial_thread():
    global serial_ok

    while True:
        try:
            with serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1) as ser:
                serial_ok = True

                while True:
                    line = ser.readline().decode("utf-8", errors="ignore").strip()
                    if not line:
                        continue

                    parsed = parse_line(line)
                    if parsed is None:
                        continue

                    with lock:
                        latest_counts["X"] = parsed["X"]
                        latest_counts["Y"] = parsed["Y"]
                        latest_counts["Z"] = parsed["Z"]

        except Exception:
            serial_ok = False
            time.sleep(1)


def counts_to_mm(counts, offsets):
    return {
        axis: (counts[axis] - offsets[axis]) * MM_PER_COUNT
        for axis in ("X", "Y", "Z")
    }


def format_mm(mm: float) -> str:
    sign = "-" if mm < 0 else ""
    abs_val = abs(mm)
    fixed = f"{abs_val:.3f}"
    int_part, dec_part = fixed.split(".")
    int_part = int_part.zfill(3)
    return f"{sign}{int_part},{dec_part}"


class MainMenuFrame(tk.Frame):
    def __init__(self, master, app, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.app = app
        self.configure(bg="#101010")
        self.value_labels = {}
        self.menu_buttons = {}
        self.status_var = tk.StringVar(value="Connessione seriale...")
        self.active_mode = tk.StringVar(value="MANUALE")
        self.build_ui()
        self.update_ui()

    def build_ui(self):
        self.grid_rowconfigure(0, weight=0)
        self.grid_rowconfigure(1, weight=1)
        self.grid_rowconfigure(2, weight=0)
        self.grid_columnconfigure(0, weight=1)

        top_frame = tk.Frame(self, bg="#101010", padx=20, pady=18)
        top_frame.grid(row=0, column=0, sticky="ew")
        for col in range(3):
            top_frame.grid_columnconfigure(col, weight=1)

        for idx, axis in enumerate(("X", "Y", "Z")):
            panel = tk.Frame(
                top_frame,
                bg="#1a1a1a",
                highlightthickness=2,
                highlightbackground="#2f2f2f",
                padx=18,
                pady=10,
            )
            panel.grid(row=0, column=idx, padx=10, sticky="nsew")

            axis_label = tk.Label(
                panel,
                text=axis,
                font=("Arial", 20, "bold"),
                fg="#cfcfcf",
                bg="#1a1a1a",
            )
            axis_label.pack(anchor="center")

            value_label = tk.Label(
                panel,
                text="000,000",
                font=("Courier New", 34, "bold"),
                fg="#ffffff",
                bg="#1a1a1a",
            )
            value_label.pack(anchor="center", pady=(6, 0))
            self.value_labels[axis] = value_label

        center_frame = tk.Frame(self, bg="#101010", padx=30, pady=10)
        center_frame.grid(row=1, column=0, sticky="nsew")
        center_frame.grid_rowconfigure(0, weight=1)
        center_frame.grid_rowconfigure(1, weight=1)
        center_frame.grid_rowconfigure(2, weight=1)
        center_frame.grid_columnconfigure(0, weight=1)
        center_frame.grid_columnconfigure(1, weight=1)

        buttons = [
            ("manuale", 0, 0),
            ("spianatura XY", 0, 1),
            ("foro grande", 1, 0),
            ("spianatura XZ", 1, 1),
            ("spianatura YZ", 2, 0),
        ]

        for text, row, col in buttons:
            btn = tk.Button(
                center_frame,
                text=text,
                font=("Arial", 26, "bold"),
                bg="#2d7ef7",
                fg="white",
                activebackground="#4d96ff",
                activeforeground="white",
                relief="flat",
                bd=0,
                padx=18,
                pady=18,
                command=lambda mode=text: self.select_mode(mode),
            )
            btn.grid(row=row, column=col, padx=18, pady=18, sticky="nsew")
            self.menu_buttons[text] = btn

        placeholder = tk.Frame(center_frame, bg="#101010")
        placeholder.grid(row=2, column=1, padx=18, pady=18, sticky="nsew")

        bottom_frame = tk.Frame(self, bg="#101010", padx=20, pady=12)
        bottom_frame.grid(row=2, column=0, sticky="ew")
        bottom_frame.grid_columnconfigure(0, weight=1)
        bottom_frame.grid_columnconfigure(1, weight=0)
        bottom_frame.grid_columnconfigure(2, weight=0)

        status_label = tk.Label(
            bottom_frame,
            textvariable=self.status_var,
            font=("Arial", 18),
            fg="#cfcfcf",
            bg="#101010",
        )
        status_label.grid(row=0, column=0, sticky="w")

        mode_label = tk.Label(
            bottom_frame,
            textvariable=self.active_mode,
            font=("Arial", 20, "bold"),
            fg="#ffffff",
            bg="#101010",
        )
        mode_label.grid(row=0, column=1, padx=20)

        exit_button = tk.Button(
            bottom_frame,
            text="ESCI",
            font=("Arial", 18, "bold"),
            bg="#aa2e25",
            fg="white",
            activebackground="#c23c32",
            activeforeground="white",
            relief="flat",
            bd=0,
            padx=18,
            pady=10,
            command=self.app.root.destroy,
        )
        exit_button.grid(row=0, column=2, sticky="e")

        self.highlight_active_button("manuale")

    def select_mode(self, mode: str):
        self.active_mode.set(mode.upper())
        self.highlight_active_button(mode)

        if mode == "spianatura XY":
            self.app.show_spianatura_xy()
        elif mode != "manuale":
            messagebox.showinfo("Modalità selezionata", f"Hai selezionato: {mode}")

    def highlight_active_button(self, active_text: str):
        for text, button in self.menu_buttons.items():
            if text == active_text:
                button.configure(bg="#1f9d55")
            else:
                button.configure(bg="#2d7ef7")

    def update_ui(self):
        values = self.app.get_axis_strings()
        for axis in ("X", "Y", "Z"):
            self.value_labels[axis].configure(text=values[axis])

        self.status_var.set("Seriale OK" if serial_ok else "Seriale non disponibile")
        self.after(REFRESH_MS, self.update_ui)


class DROApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("DRO Fresa")
        self.root.configure(bg="#101010")
        self.root.attributes("-fullscreen", True)
        self.root.bind("<Escape>", self.exit_fullscreen)
        self.root.bind("<F11>", self.toggle_fullscreen)
        self.current_frame = None
        self.show_main_menu()

    def get_axis_strings(self):
        with lock:
            counts = latest_counts.copy()
            offsets = zero_offsets.copy()

        mm_values = counts_to_mm(counts, offsets)
        return {axis: format_mm(mm_values[axis]) for axis in ("X", "Y", "Z")}

    def show_frame(self, frame: tk.Frame):
        if self.current_frame is not None:
            self.current_frame.destroy()
        self.current_frame = frame
        self.current_frame.pack(fill="both", expand=True)

    def show_main_menu(self):
        self.show_frame(MainMenuFrame(self.root, self))

    def show_spianatura_xy(self):
        self.show_frame(
            SpianaturaXYFrame(
                self.root,
                on_back=self.show_main_menu,
                get_axis_values=self.get_axis_strings,
            )
        )

    def toggle_fullscreen(self, event=None):
        current = self.root.attributes("-fullscreen")
        self.root.attributes("-fullscreen", not current)

    def exit_fullscreen(self, event=None):
        self.root.attributes("-fullscreen", False)


if __name__ == "__main__":
    t = threading.Thread(target=serial_thread, daemon=True)
    t.start()

    root = tk.Tk()
    app = DROApp(root)
    root.mainloop()
