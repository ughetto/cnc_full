import tkinter as tk

from keypad_numeric_overlay import ask_numeric_value
from settings_store import SettingsError


class SettingsFrame(tk.Frame):
    def __init__(self, master, on_back, on_save, initial_values, initial_notice="", bg="#101010"):
        super().__init__(master, bg=bg)
        self.on_back = on_back
        self.on_save = on_save
        self.bg = bg
        self.value_vars = {
            axis: tk.StringVar(value=self.format_value(initial_values.get(axis, 0.0)))
            for axis in ("X", "Y", "Z")
        }
        self.notice_var = tk.StringVar(value=initial_notice)
        self.notice_label = None
        self.build_ui()

    @staticmethod
    def format_value(value):
        try:
            return f"{float(value):07.3f}".replace(".", ",")
        except (TypeError, ValueError):
            return "000,000"

    def build_ui(self):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        tk.Label(
            self,
            text="SETTINGS",
            font=("Arial", 30, "bold"),
            fg="white",
            bg=self.bg,
        ).grid(row=0, column=0, pady=(45, 20))

        panel = tk.Frame(
            self,
            bg="#151515",
            highlightthickness=2,
            highlightbackground="#333333",
            padx=50,
            pady=35,
        )
        panel.grid(row=1, column=0)

        tk.Label(
            panel,
            text="IMPULSI PER MILLIMETRO",
            font=("Arial", 22, "bold"),
            fg="white",
            bg="#151515",
        ).grid(row=0, column=0, columnspan=2, pady=(0, 25))

        for row, axis in enumerate(("X", "Y", "Z"), start=1):
            tk.Label(
                panel,
                text=f"ASSE {axis}",
                font=("Arial", 22, "bold"),
                fg="white",
                bg="#151515",
            ).grid(row=row, column=0, sticky="e", padx=(0, 30), pady=12)

            entry = tk.Entry(
                panel,
                textvariable=self.value_vars[axis],
                font=("Courier New", 24, "bold"),
                width=10,
                justify="center",
                state="readonly",
                readonlybackground="#1a1a1a",
                fg="white",
                relief="flat",
                bd=0,
            )
            entry.grid(row=row, column=1, pady=12)
            entry.bind("<Button-1>", lambda event, a=axis: self.open_keypad(a))

        self.notice_label = tk.Label(
            self,
            textvariable=self.notice_var,
            font=("Arial", 16, "bold"),
            fg="#f0b84b",
            bg=self.bg,
            wraplength=1000,
            justify="center",
        )
        self.notice_label.grid(row=2, column=0, padx=30, pady=18)

        actions = tk.Frame(self, bg=self.bg)
        actions.grid(row=3, column=0, pady=(0, 35))

        tk.Button(
            actions,
            text="INDIETRO",
            command=self.on_back,
            font=("Arial", 18, "bold"),
            bg="#444444",
            fg="white",
            activebackground="#666666",
            activeforeground="white",
            relief="flat",
            bd=0,
            padx=25,
            pady=12,
        ).grid(row=0, column=0, padx=12)

        tk.Button(
            actions,
            text="SALVA",
            command=self.save,
            font=("Arial", 20, "bold"),
            bg="#1f9d55",
            fg="white",
            activebackground="#27b964",
            activeforeground="white",
            relief="flat",
            bd=0,
            padx=35,
            pady=12,
        ).grid(row=0, column=1, padx=12)

    def open_keypad(self, axis):
        new_value = ask_numeric_value(
            self,
            initial_value=self.value_vars[axis].get(),
            title=f"Impulsi/mm asse {axis}",
        )
        if new_value is not None:
            self.value_vars[axis].set(new_value)
        return "break"

    def save(self):
        values = {axis: var.get() for axis, var in self.value_vars.items()}
        try:
            saved_values = self.on_save(values)
        except SettingsError as exc:
            self.notice_var.set(str(exc))
            self.notice_label.configure(fg="#ff6b6b")
            return

        for axis, value in saved_values.items():
            self.value_vars[axis].set(self.format_value(value))
        self.notice_var.set("Settings salvati e già attivi nell'applicazione.")
        self.notice_label.configure(fg="#7CFF6B")
