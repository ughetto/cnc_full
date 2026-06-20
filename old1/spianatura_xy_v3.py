import tkinter as tk
from keypad_numeric_overlay import ask_numeric_value


class SpianaturaXYFrame(tk.Frame):
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
        ]

        self.build_ui()
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
        body_frame.grid_columnconfigure(0, weight=0)
        body_frame.grid_columnconfigure(1, weight=1)

        title_label = tk.Label(
            body_frame,
            text="SPIANATURA XY",
            font=("Arial", 28, "bold"),
            fg="#ffffff",
            bg="#101010",
        )
        title_label.grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 24))

        for row, label_text in enumerate(self.fields, start=1):
            label = tk.Label(
                body_frame,
                text=label_text,
                font=("Arial", 24, "bold"),
                fg="#ffffff",
                bg="#101010",
                anchor="w",
            )
            label.grid(row=row, column=0, sticky="w", padx=(0, 30), pady=16)

            var = tk.StringVar(value="000,000")
            entry = tk.Entry(
                body_frame,
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

        reset_button = tk.Button(
            bottom_frame,
            text="AZZERA CAMPI",
            font=("Arial", 18, "bold"),
            bg="#aa7a18",
            fg="white",
            activebackground="#c5921e",
            activeforeground="white",
            relief="flat",
            bd=0,
            padx=18,
            pady=10,
            command=self.reset_fields,
        )
        reset_button.grid(row=0, column=1, padx=10)

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

    def reset_fields(self):
        for var in self.entry_vars.values():
            var.set("000,000")

    def update_axis_values(self):
        values = self.get_axis_values()
        for axis in ("X", "Y", "Z"):
            self.value_labels[axis].configure(text=values.get(axis, "000,000"))
        self.after(self.refresh_ms, self.update_axis_values)
