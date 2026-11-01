import tkinter as tk


class TestAutoFrame(tk.Frame):
    def __init__(self, master, on_back, on_move, on_two_segments, on_stop, on_reset, on_status,
                 get_state, pulses_per_mm, provisional_settings=False, bg="#101010"):
        super().__init__(master, bg=bg)
        self.on_back = on_back
        self.on_move = on_move
        self.on_two_segments = on_two_segments
        self.on_stop = on_stop
        self.on_reset = on_reset
        self.on_status = on_status
        self.get_state = get_state
        self.state_var = tk.StringVar(value="Controller: IDLE")
        self.build_ui(pulses_per_mm, provisional_settings)

    def build_ui(self, pulses_per_mm, provisional_settings):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(2, weight=1)

        tk.Label(
            self,
            text="TEST AUTO TEMPORANEO",
            font=("Arial", 28, "bold"),
            fg="#ffcf5a",
            bg=self["bg"],
        ).grid(row=0, column=0, pady=(18, 4))

        warning = "SOLO COLLAUDO ELETTRICO — DRIVER SCOLLEGATI — ORIGINE TEST (0,0,0)"
        if provisional_settings:
            warning += " — IMPULSI/MM PROVVISORI"
        tk.Label(
            self,
            text=warning,
            font=("Arial", 15, "bold"),
            fg="#ff6b6b",
            bg=self["bg"],
        ).grid(row=1, column=0, pady=(0, 10))

        body = tk.Frame(self, bg=self["bg"], padx=20)
        body.grid(row=2, column=0, sticky="nsew")
        body.grid_columnconfigure(0, weight=0)
        body.grid_columnconfigure(1, weight=1)
        body.grid_rowconfigure(0, weight=1)

        controls = tk.Frame(body, bg="#151515", padx=18, pady=14)
        controls.grid(row=0, column=0, sticky="nsw", padx=(0, 18))

        moves = (
            ("X +100 STEP", (100, 0, 0)),
            ("X -100 STEP", (-100, 0, 0)),
            ("Y +100 STEP", (0, 100, 0)),
            ("Y -100 STEP", (0, -100, 0)),
            ("Z +100 STEP", (0, 0, 100)),
            ("Z -100 STEP", (0, 0, -100)),
            ("XY 100 / 50", (100, 50, 0)),
            ("XY 100 / 25", (100, 25, 0)),
            ("XY 100 / 75", (100, 75, 0)),
        )
        for index, (label, delta) in enumerate(moves):
            tk.Button(
                controls,
                text=label,
                command=lambda d=delta: self.run_action(self.on_move, d),
                font=("Arial", 16, "bold"),
                width=16,
                bg="#2d7ef7",
                fg="white",
                activebackground="#4d96ff",
                activeforeground="white",
                relief="flat",
                bd=0,
                padx=10,
                pady=10,
            ).grid(row=index // 2, column=index % 2, padx=7, pady=7, sticky="ew")

        tk.Button(
            controls,
            text="2 SEG X+50 / X+50",
            command=lambda: self.run_action(self.on_two_segments),
            font=("Arial", 16, "bold"),
            bg="#6b4db7",
            fg="white",
            activebackground="#8264ce",
            activeforeground="white",
            relief="flat",
            bd=0,
            padx=10,
            pady=10,
        ).grid(row=4, column=1, padx=7, pady=7, sticky="ew")

        actions = tk.Frame(controls, bg="#151515")
        actions.grid(row=5, column=0, columnspan=2, pady=(14, 0))
        for column, (label, callback, color) in enumerate((
            ("STOP", self.on_stop, "#aa2e25"),
            ("RESET AUTO", self.on_reset, "#aa7a18"),
            ("STATUS", self.on_status, "#555555"),
        )):
            tk.Button(
                actions,
                text=label,
                command=lambda cb=callback: self.run_action(cb),
                font=("Arial", 15, "bold"),
                bg=color,
                fg="white",
                activeforeground="white",
                relief="flat",
                bd=0,
                padx=13,
                pady=10,
            ).grid(row=0, column=column, padx=5)

        info = tk.Frame(body, bg="#151515", padx=16, pady=12)
        info.grid(row=0, column=1, sticky="nsew")
        info.grid_columnconfigure(0, weight=1)
        info.grid_rowconfigure(3, weight=1)

        tk.Label(info, textvariable=self.state_var, font=("Arial", 18, "bold"),
                 fg="#7CFF6B", bg="#151515", anchor="w").grid(row=0, column=0, sticky="ew")
        scales = "  ".join(f"{axis}={float(pulses_per_mm[axis]):g}"
                           for axis in ("X", "Y", "Z"))
        tk.Label(info, text=f"Impulsi/mm: {scales} | Cadenza dominante: circa 100 step/s",
                 font=("Arial", 13), fg="#cfcfcf", bg="#151515", anchor="w").grid(
                     row=1, column=0, sticky="ew", pady=(5, 10))
        tk.Label(info, text="LOG PROTOCOLLO AUTO", font=("Arial", 14, "bold"),
                 fg="white", bg="#151515", anchor="w").grid(row=2, column=0, sticky="ew")

        self.log = tk.Text(info, font=("Courier New", 11), bg="#080808", fg="#d8e8ff",
                           insertbackground="white", wrap="word", state="disabled", height=16)
        self.log.grid(row=3, column=0, sticky="nsew", pady=(5, 0))

        footer = tk.Frame(self, bg=self["bg"], padx=20, pady=12)
        footer.grid(row=3, column=0, sticky="ew")
        tk.Button(footer, text="INDIETRO", command=self.go_back,
                  font=("Arial", 17, "bold"), bg="#444444", fg="white",
                  activebackground="#666666", activeforeground="white",
                  relief="flat", bd=0, padx=24, pady=9).pack(side="right")

    def run_action(self, callback, *args):
        try:
            message = callback(*args)
        except Exception as exc:
            self.append_line(f"GUI ERROR: {exc}")
            return
        if message:
            self.append_line(str(message))
        self.refresh_state()

    def append_line(self, line):
        self.log.configure(state="normal")
        self.log.insert("end", f"{line}\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def refresh_state(self):
        self.state_var.set(f"Controller: {self.get_state()}")

    def go_back(self):
        state = self.get_state()
        if state != "IDLE":
            self.append_line(f"INDIETRO bloccato: eseguire STOP e RESET (stato {state}).")
            return
        self.on_back()
