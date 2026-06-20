import tkinter as tk


class ManualeFrame(tk.Frame):
    # Valori massimi touch in step/s.
    # XY raddoppiato rispetto a v15, Z quadruplicato rispetto a v15.
    XY_MAX_SPS = 1600.0
    Z_MAX_SPS = 6400.0

    def __init__(self, master, on_back, get_axis_values, on_manual_command, bg="#101010"):
        super().__init__(master, bg=bg)
        self.on_back = on_back
        self.get_axis_values = get_axis_values
        self.on_manual_command = on_manual_command
        self.bg = bg
        self.xy_x = 0.0
        self.xy_y = 0.0
        self.pressed_xy = False
        self.axis_labels = {}
        self.x_speed_mm_s = tk.DoubleVar(value=10.0)
        self.y_speed_mm_s = tk.DoubleVar(value=10.0)
        self.build_ui()
        self.refresh_axes()
        self.send_current_command(send_semi=True)

    def build_ui(self):
        self.grid_rowconfigure(0, minsize=76, weight=0)
        self.grid_rowconfigure(1, weight=1)
        self.grid_rowconfigure(2, minsize=58, weight=0)
        self.grid_columnconfigure(0, weight=1)

        header = tk.Frame(self, bg=self.bg, padx=14, pady=6)
        header.grid(row=0, column=0, sticky="ew")
        for i in range(3):
            header.grid_columnconfigure(i, weight=1)

        for idx, axis in enumerate(("X", "Y", "Z")):
            box = tk.Frame(header, bg="#1a1a1a", highlightbackground="#333333",
                           highlightthickness=2, padx=12, pady=4)
            box.grid(row=0, column=idx, sticky="ew", padx=6)
            tk.Label(box, text=axis, font=("Arial", 14, "bold"),
                     fg="#cfcfcf", bg="#1a1a1a").pack()
            lbl = tk.Label(box, text="000,000", font=("Courier New", 25, "bold"),
                           fg="white", bg="#1a1a1a")
            lbl.pack()
            self.axis_labels[axis] = lbl

        body = tk.Frame(self, bg=self.bg, padx=16, pady=4)
        body.grid(row=1, column=0, sticky="nsew")
        body.grid_columnconfigure(0, weight=1)
        body.grid_columnconfigure(1, weight=0)
        body.grid_columnconfigure(2, weight=1)
        body.grid_rowconfigure(0, weight=1)

        xy_panel = tk.Frame(body, bg="#151515", highlightbackground="#3a3a3a",
                            highlightthickness=2, padx=10, pady=8)
        xy_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        tk.Label(xy_panel, text="CONTROLLO X / Y", font=("Arial", 17, "bold"),
                 fg="white", bg="#151515").pack(pady=(0, 5))

        self.xy_size = 405
        self.xy_canvas = tk.Canvas(xy_panel, width=self.xy_size, height=self.xy_size,
                                   bg="#202020", highlightthickness=0)
        self.xy_canvas.pack(anchor="center")
        self.xy_canvas.bind("<ButtonPress-1>", self.xy_press)
        self.xy_canvas.bind("<B1-Motion>", self.xy_motion)
        self.xy_canvas.bind("<ButtonRelease-1>", self.xy_release)
        self.draw_xy()

        xy_info = tk.Frame(xy_panel, bg="#151515")
        xy_info.pack(fill="x", pady=(6, 0))
        self.xy_status = tk.Label(xy_info, text="X: 0  Y: 0 step/s",
                                  font=("Arial", 13, "bold"),
                                  fg="#dddddd", bg="#151515")
        self.xy_status.pack(side="left")
        tk.Button(xy_info, text="STOP XY", command=self.stop_xy,
                  font=("Arial", 12, "bold"), bg="#aa2e25", fg="white",
                  activebackground="#c23c32", activeforeground="white",
                  relief="flat", bd=0, padx=16, pady=6).pack(side="right")

        tk.Frame(body, bg="#101010", width=8).grid(row=0, column=1, sticky="ns")

        speed_panel = tk.Frame(body, bg="#151515", highlightbackground="#3a3a3a",
                               highlightthickness=2, padx=18, pady=16)
        speed_panel.grid(row=0, column=2, sticky="nsew", padx=(10, 0))
        speed_panel.grid_columnconfigure(0, weight=1)

        tk.Label(speed_panel, text="VELOCITA SEMIAUTOMATICA",
                 font=("Arial", 18, "bold"), fg="white", bg="#151515").grid(row=0, column=0, pady=(0, 28))

        tk.Label(speed_panel, text="ASSE X", font=("Arial", 17, "bold"),
                 fg="#ffffff", bg="#151515").grid(row=1, column=0, pady=(0, 4))
        self.x_speed_label = tk.Label(speed_panel, text="10,0 mm/s", font=("Arial", 18, "bold"),
                                      fg="#dddddd", bg="#151515")
        self.x_speed_label.grid(row=2, column=0, pady=(0, 8))
        x_slider = tk.Scale(speed_panel, from_=1, to=20, resolution=0.5,
                            orient="horizontal", length=360, variable=self.x_speed_mm_s,
                            command=self.speed_changed, showvalue=False,
                            bg="#151515", fg="white", troughcolor="#404040",
                            activebackground="#2d7ef7", highlightthickness=0,
                            font=("Arial", 12))
        x_slider.grid(row=3, column=0, pady=(0, 34))

        tk.Label(speed_panel, text="ASSE Y", font=("Arial", 17, "bold"),
                 fg="#ffffff", bg="#151515").grid(row=4, column=0, pady=(0, 4))
        self.y_speed_label = tk.Label(speed_panel, text="10,0 mm/s", font=("Arial", 18, "bold"),
                                      fg="#dddddd", bg="#151515")
        self.y_speed_label.grid(row=5, column=0, pady=(0, 8))
        y_slider = tk.Scale(speed_panel, from_=1, to=20, resolution=0.5,
                            orient="horizontal", length=360, variable=self.y_speed_mm_s,
                            command=self.speed_changed, showvalue=False,
                            bg="#151515", fg="white", troughcolor="#404040",
                            activebackground="#2d7ef7", highlightthickness=0,
                            font=("Arial", 12))
        y_slider.grid(row=6, column=0, pady=(0, 20))

        tk.Label(speed_panel, text="I commutatori hardware X+/X- e Y+/Y- usano queste velocita.",
                 font=("Arial", 12), fg="#bbbbbb", bg="#151515", wraplength=360,
                 justify="center").grid(row=7, column=0, pady=(20, 0))

        footer = tk.Frame(self, bg=self.bg, padx=18, pady=6)
        footer.grid(row=2, column=0, sticky="ew")
        footer.grid_columnconfigure(0, weight=1)
        footer.grid_columnconfigure(1, weight=0)
        footer.grid_columnconfigure(2, weight=1)

        tk.Button(footer, text="INDIETRO", command=self.go_back,
                  font=("Arial", 16, "bold"), bg="#444444", fg="white",
                  activebackground="#666666", activeforeground="white",
                  relief="flat", bd=0, padx=24, pady=8).grid(row=0, column=0, sticky="w")

        tk.Button(footer, text="STOP TUTTO", command=self.stop_all,
                  font=("Arial", 17, "bold"), bg="#aa2e25", fg="white",
                  activebackground="#c23c32", activeforeground="white",
                  relief="flat", bd=0, padx=34, pady=9).grid(row=0, column=1)

    @staticmethod
    def clamp(v, lo=-1.0, hi=1.0):
        return max(lo, min(hi, v))

    def draw_xy(self):
        c = self.xy_canvas
        s = self.xy_size
        c.delete("all")
        margin = 16
        center = s / 2
        c.create_rectangle(margin, margin, s - margin, s - margin,
                           outline="#707070", width=3)
        c.create_line(center, margin, center, s - margin, fill="#505050", width=2)
        c.create_line(margin, center, s - margin, center, fill="#505050", width=2)
        c.create_text(center, margin + 18, text="+Y", fill="#dddddd", font=("Arial", 13, "bold"))
        c.create_text(center, s - margin - 18, text="-Y", fill="#dddddd", font=("Arial", 13, "bold"))
        c.create_text(s - margin - 22, center - 16, text="+X", fill="#dddddd", font=("Arial", 13, "bold"))
        c.create_text(margin + 22, center - 16, text="-X", fill="#dddddd", font=("Arial", 13, "bold"))
        usable = (s - 2 * margin) / 2
        px = center + self.xy_x * usable
        py = center - self.xy_y * usable
        c.create_oval(px - 18, py - 18, px + 18, py + 18,
                      fill="#2d7ef7", outline="white", width=2)

    def xy_from_event(self, event):
        s = self.xy_size
        margin = 16
        center = s / 2
        usable = (s - 2 * margin) / 2
        # Limite quadrato: X e Y sono clampati indipendentemente.
        self.xy_x = self.clamp((event.x - center) / usable)
        self.xy_y = self.clamp((center - event.y) / usable)
        self.draw_xy()
        self.send_current_command()

    def xy_press(self, event):
        self.pressed_xy = True
        self.xy_from_event(event)

    def xy_motion(self, event):
        if self.pressed_xy:
            self.xy_from_event(event)

    def xy_release(self, event):
        self.pressed_xy = False
        self.xy_x = 0.0
        self.xy_y = 0.0
        self.draw_xy()
        self.send_current_command()

    def stop_xy(self):
        self.xy_x = 0.0
        self.xy_y = 0.0
        self.draw_xy()
        self.send_current_command()

    def stop_all(self):
        self.xy_x = 0.0
        self.xy_y = 0.0
        self.draw_xy()
        self.send_current_command()

    def go_back(self):
        self.stop_all()
        self.on_back()

    def speed_changed(self, event=None):
        self.update_speed_labels()
        self.send_current_command(send_semi=True)

    def update_speed_labels(self):
        x = float(self.x_speed_mm_s.get())
        y = float(self.y_speed_mm_s.get())
        self.x_speed_label.configure(text=(f"{x:.1f} mm/s").replace(".", ","))
        self.y_speed_label.configure(text=(f"{y:.1f} mm/s").replace(".", ","))


    def send_current_command(self, send_semi=False):
        x_sps = self.xy_x * self.XY_MAX_SPS
        y_sps = self.xy_y * self.XY_MAX_SPS
        z_sps = 0.0

        data = {"x": x_sps, "y": y_sps, "z": z_sps}

        if send_semi:
            # Gli slider sono 1..20 mm/s. Qui li convertiamo in step/s.
            # X e Y sono separati; Z non ha deviatore hardware e quindi non viene inviato.
            x_semi_sps = (float(self.x_speed_mm_s.get()) / 20.0) * self.XY_MAX_SPS
            y_semi_sps = (float(self.y_speed_mm_s.get()) / 20.0) * self.XY_MAX_SPS
            data["semi_x_sps"] = x_semi_sps
            data["semi_y_sps"] = y_semi_sps

        self.xy_status.configure(text=f"X: {x_sps:.0f}  Y: {y_sps:.0f} step/s")
        self.on_manual_command(data)

    def refresh_axes(self):
        try:
            values = self.get_axis_values()
            for axis, label in self.axis_labels.items():
                label.configure(text=values.get(axis, "000,000"))
        except Exception:
            pass
        self.after(100, self.refresh_axes)
