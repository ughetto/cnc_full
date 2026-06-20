import tkinter as tk
from keypad_numeric_overlay import ask_numeric_value


class SpianaturaXYFrame(tk.Frame):
    DEFAULT_VALUES = {
        "totale spianatura [mm]": "010,000",
        "diametro utensile [mm]": "010,000",
        "velocità di avanzamento [mm/s]": "005,000",
        "profondità di passata [mm]": "000,150",
        "sovrapposizione": "002,000",
    }

    def __init__(self, master, on_back, get_axis_values, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.configure(bg="#101010")
        self.on_back = on_back
        self.get_axis_values = get_axis_values
        self.value_labels = {}
        self.entry_vars = {}
        self.entry_widgets = {}
        self.refresh_ms = 100

        self.fields = [
            "totale spianatura [mm]",
            "diametro utensile [mm]",
            "velocità di avanzamento [mm/s]",
            "profondità di passata [mm]",
            "sovrapposizione",
        ]

        self.point_states = {
            "inizio": {"locked": False},
            "fine": {"locked": False},
        }
        self.point_vars = {
            "inizio": {"X": tk.StringVar(value="000.000"), "Y": tk.StringVar(value="000.000")},
            "fine": {"X": tk.StringVar(value="000.000"), "Y": tk.StringVar(value="000.000")},
        }
        self.point_buttons = {}

        self.build_ui()
        self.apply_defaults()
        self.update_axis_values()

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

        body_frame = tk.Frame(self, bg="#101010", padx=20, pady=10)
        body_frame.grid(row=1, column=0, sticky="nsew")
        body_frame.grid_columnconfigure(0, weight=1)
        body_frame.grid_columnconfigure(1, weight=0)

        title_label = tk.Label(
            body_frame,
            text="SPIANATURA XY",
            font=("Arial", 28, "bold"),
            fg="#ffffff",
            bg="#101010",
        )
        title_label.grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 24))

        left_panel = tk.Frame(body_frame, bg="#101010")
        left_panel.grid(row=1, column=0, sticky="nw")
        left_panel.grid_columnconfigure(0, weight=0)
        left_panel.grid_columnconfigure(1, weight=1)

        for row, label_text in enumerate(self.fields):
            label = tk.Label(
                left_panel,
                text=label_text,
                font=("Arial", 24, "bold"),
                fg="#ffffff",
                bg="#101010",
                anchor="w",
            )
            label.grid(row=row, column=0, sticky="w", padx=(0, 30), pady=16)

            var = tk.StringVar(value=self.DEFAULT_VALUES[label_text])
            entry = tk.Entry(
                left_panel,
                textvariable=var,
                font=("Courier New", 24, "bold"),
                width=8,
                justify="center",
                bg="#1a1a1a",
                fg="#ffffff",
                insertbackground="#ffffff",
                relief="flat",
                bd=0,
                state="readonly",
                readonlybackground="#1a1a1a",
            )
            entry.grid(row=row, column=1, sticky="w", pady=16)
            entry.bind("<Button-1>", lambda event, f=label_text: self.open_keypad(f))

            self.entry_vars[label_text] = var
            self.entry_widgets[label_text] = entry

        right_panel = tk.Frame(body_frame, bg="#101010")
        right_panel.grid(row=1, column=1, sticky="ne", padx=(40, 10))

        self.build_point_block(right_panel, "inizio", 0)
        self.build_point_block(right_panel, "fine", 1)

        bottom_frame = tk.Frame(self, bg="#101010", padx=20, pady=16)
        bottom_frame.grid(row=2, column=0, sticky="ew")
        bottom_frame.grid_columnconfigure(0, weight=1)
        bottom_frame.grid_columnconfigure(1, weight=0)
        bottom_frame.grid_columnconfigure(2, weight=0)

        hint_label = tk.Label(
            bottom_frame,
            text="Tocca un campo per inserire il valore",
            font=("Arial", 18),
            fg="#cfcfcf",
            bg="#101010",
        )
        hint_label.grid(row=0, column=0, sticky="w")

        default_button = tk.Button(
            bottom_frame,
            text="DEFAULT",
            font=("Arial", 18, "bold"),
            bg="#aa7a18",
            fg="white",
            activebackground="#c5921e",
            activeforeground="white",
            relief="flat",
            bd=0,
            padx=18,
            pady=10,
            command=self.apply_defaults,
        )
        default_button.grid(row=0, column=1, padx=10)

        back_button = tk.Button(
            bottom_frame,
            text="INDIETRO",
            font=("Arial", 18, "bold"),
            bg="#aa2e25",
            fg="white",
            activebackground="#c23c32",
            activeforeground="white",
            relief="flat",
            bd=0,
            padx=18,
            pady=10,
            command=self.on_back,
        )
        back_button.grid(row=0, column=2, sticky="e")

    def build_point_block(self, parent, name: str, row: int):
        block = tk.Frame(
            parent,
            bg="#151515",
            highlightthickness=2,
            highlightbackground="#2f2f2f",
            padx=18,
            pady=18,
        )
        block.grid(row=row, column=0, sticky="e", pady=(0, 24))

        button = tk.Button(
            block,
            text=name.upper(),
            command=lambda n=name: self.toggle_point(n),
            font=("Arial", 20, "bold"),
            width=8,
            height=3,
            bg="#1f9d55",
            fg="white",
            activeforeground="white",
            activebackground="#27b964",
            relief="flat",
            bd=0,
        )
        button.grid(row=0, column=0, rowspan=2, padx=(0, 20))
        self.point_buttons[name] = button

        self.build_coord_row(block, 0, "X", self.point_vars[name]["X"])
        self.build_coord_row(block, 1, "Y", self.point_vars[name]["Y"])

    def build_coord_row(self, parent, row: int, axis: str, var: tk.StringVar):
        label = tk.Label(
            parent,
            text=f"{axis}=",
            font=("Arial", 20, "bold"),
            fg="#ffffff",
            bg="#151515",
            anchor="e",
        )
        label.grid(row=row, column=1, sticky="e", padx=(0, 8), pady=6)

        value = tk.Label(
            parent,
            textvariable=var,
            font=("Courier New", 22, "bold"),
            fg="#ffffff",
            bg="#1a1a1a",
            width=8,
            anchor="center",
            padx=8,
            pady=6,
        )
        value.grid(row=row, column=2, sticky="w", pady=6)

    def open_keypad(self, field_name: str):
        current_value = self.entry_vars[field_name].get()
        new_value = ask_numeric_value(
            self,
            initial_value=current_value,
            title=field_name,
        )
        if new_value is not None:
            self.entry_vars[field_name].set(new_value)
        return "break"

    def apply_defaults(self):
        for field_name, default_value in self.DEFAULT_VALUES.items():
            if field_name in self.entry_vars:
                self.entry_vars[field_name].set(default_value)

    def comma_to_point(self, text: str) -> str:
        return (text or "000,000").replace(",", ".")

    def sync_live_points(self, values: dict):
        live_x = self.comma_to_point(values.get("X", "000,000"))
        live_y = self.comma_to_point(values.get("Y", "000,000"))

        for name, state in self.point_states.items():
            if not state["locked"]:
                self.point_vars[name]["X"].set(live_x)
                self.point_vars[name]["Y"].set(live_y)

    def toggle_point(self, name: str):
        state = self.point_states[name]
        state["locked"] = not state["locked"]

        button = self.point_buttons[name]
        if state["locked"]:
            button.configure(
                bg="#aa2e25",
                activebackground="#c23c32",
            )
        else:
            button.configure(
                bg="#1f9d55",
                activebackground="#27b964",
            )

    def update_axis_values(self):
        values = self.get_axis_values()
        for axis in ("X", "Y", "Z"):
            self.value_labels[axis].configure(text=values.get(axis, "000,000"))

        self.sync_live_points(values)
        self.after(self.refresh_ms, self.update_axis_values)
