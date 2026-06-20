import tkinter as tk


class NumericKeypadOverlay:
    def __init__(self, parent, initial_value="000,000", title="Inserimento valore"):
        self.parent = parent
        self.result = None
        self._cleared_on_first_digit = False
        self._title_text = title
        self._done = tk.BooleanVar(value=False)
        self.value_var = tk.StringVar(value=initial_value)

        self.overlay = tk.Frame(parent, bg="#000000")
        self.overlay.place(relx=0, rely=0, relwidth=1, relheight=1)

        self.dialog = tk.Frame(
            self.overlay,
            bg="#1a1a1a",
            highlightthickness=3,
            highlightbackground="#3a3a3a",
            padx=24,
            pady=24,
        )
        self.dialog.place(relx=0.5, rely=0.5, anchor="center")

        self.build_ui()
        self.dialog.focus_set()
        self.dialog.bind("<Escape>", lambda e: self.on_cancel())

    def build_ui(self):
        title_label = tk.Label(
            self.dialog,
            text=self._title_text,
            font=("Arial", 22, "bold"),
            fg="#ffffff",
            bg="#1a1a1a",
        )
        title_label.pack(pady=(0, 12))

        value_label = tk.Label(
            self.dialog,
            textvariable=self.value_var,
            font=("Courier New", 34, "bold"),
            fg="#ffffff",
            bg="#101010",
            width=10,
            padx=10,
            pady=12,
        )
        value_label.pack(fill="x", pady=(0, 18))

        grid = tk.Frame(self.dialog, bg="#1a1a1a")
        grid.pack()

        buttons = [
            ("7", 0, 0), ("8", 0, 1), ("9", 0, 2),
            ("4", 1, 0), ("5", 1, 1), ("6", 1, 2),
            ("1", 2, 0), ("2", 2, 1), ("3", 2, 2),
            (".", 3, 0), ("0", 3, 1), ("⌫", 3, 2),
        ]

        for text, row, col in buttons:
            btn = tk.Button(
                grid,
                text=text,
                command=lambda t=text: self.on_button(t),
                font=("Arial", 24, "bold"),
                bg="#2d7ef7" if text != "⌫" else "#aa7a18",
                fg="white",
                activeforeground="white",
                activebackground="#4d96ff" if text != "⌫" else "#c5921e",
                relief="flat",
                bd=0,
                width=6,
                height=2,
            )
            btn.grid(row=row, column=col, padx=8, pady=8, sticky="nsew")

        action_row = tk.Frame(self.dialog, bg="#1a1a1a")
        action_row.pack(fill="x", pady=(18, 0))
        action_row.grid_columnconfigure(0, weight=1)
        action_row.grid_columnconfigure(1, weight=1)

        cancel_btn = tk.Button(
            action_row,
            text="CANCEL",
            command=self.on_cancel,
            font=("Arial", 22, "bold"),
            bg="#aa2e25",
            fg="white",
            activeforeground="white",
            activebackground="#c23c32",
            relief="flat",
            bd=0,
            padx=18,
            pady=12,
        )
        cancel_btn.grid(row=0, column=0, padx=(0, 8), sticky="ew")

        ok_btn = tk.Button(
            action_row,
            text="OK",
            command=self.on_ok,
            font=("Arial", 22, "bold"),
            bg="#1f9d55",
            fg="white",
            activeforeground="white",
            activebackground="#27b964",
            relief="flat",
            bd=0,
            padx=18,
            pady=12,
        )
        ok_btn.grid(row=0, column=1, padx=(8, 0), sticky="ew")

    def on_button(self, text: str):
        if text == ".":
            self.insert_decimal_separator()
        elif text == "⌫":
            self.backspace()
        else:
            self.insert_digit(text)

    def insert_digit(self, digit: str):
        current = self.value_var.get().replace(".", ",")
        if not self._cleared_on_first_digit:
            current = ""
            self._cleared_on_first_digit = True

        if "," in current:
            int_part, dec_part = current.split(",", 1)
            if len(dec_part) >= 3:
                return
            dec_part += digit
            self.value_var.set(f"{int_part},{dec_part}")
        else:
            if len(current) >= 3:
                return
            self.value_var.set(current + digit)

    def insert_decimal_separator(self):
        current = self.value_var.get().replace(".", ",")
        if not self._cleared_on_first_digit:
            current = ""
            self._cleared_on_first_digit = True
        if "," in current:
            return
        if current == "":
            current = "0"
        self.value_var.set(current + ",")

    def backspace(self):
        current = self.value_var.get().replace(".", ",")
        if not self._cleared_on_first_digit:
            current = ""
            self._cleared_on_first_digit = True
        if current:
            current = current[:-1]
        self.value_var.set(current)

    def normalize_value(self, raw: str) -> str:
        raw = (raw or "").strip().replace(".", ",")
        if raw == "":
            return "000,000"

        if "," in raw:
            int_part, dec_part = raw.split(",", 1)
        else:
            int_part, dec_part = raw, ""

        int_part = "".join(ch for ch in int_part if ch.isdigit())[:3]
        dec_part = "".join(ch for ch in dec_part if ch.isdigit())[:3]

        if int_part == "":
            int_part = "0"

        return f"{int_part.zfill(3)},{dec_part.ljust(3, '0')}"

    def on_ok(self):
        self.result = self.normalize_value(self.value_var.get())
        self.close()

    def on_cancel(self):
        self.result = None
        self.close()

    def close(self):
        self.overlay.destroy()
        self._done.set(True)


def ask_numeric_value(parent, initial_value="000,000", title="Inserimento valore"):
    keypad = NumericKeypadOverlay(parent, initial_value=initial_value, title=title)
    parent.wait_variable(keypad._done)
    return keypad.result
