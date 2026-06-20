import tkinter as tk

class ManualeFrame(tk.Frame):
    XY_MAX_SPS = 800.0
    Z_MAX_SPS = 1600.0

    def __init__(self, master, on_back, get_axis_values, on_manual_command, bg="#101010"):
        super().__init__(master, bg=bg)
        self.on_back = on_back
        self.get_axis_values = get_axis_values
        self.on_manual_command = on_manual_command
        self.bg = bg
        self.xy_x = 0.0
        self.xy_y = 0.0
        self.z_value = 0.0
        self.pressed_xy = False
        self.pressed_z = False
        self.axis_labels = {}
        self.build_ui()
        self.refresh_axes()
        self.send_current_command()

    def build_ui(self):
        self.grid_rowconfigure(0, minsize=86, weight=0)
        self.grid_rowconfigure(1, weight=1)
        self.grid_rowconfigure(2, minsize=64, weight=0)
        self.grid_columnconfigure(0, weight=1)

        header = tk.Frame(self, bg=self.bg, padx=16, pady=8)
        header.grid(row=0, column=0, sticky="ew")
        for i in range(3):
            header.grid_columnconfigure(i, weight=1)

        for idx, axis in enumerate(("X", "Y", "Z")):
            box = tk.Frame(header, bg="#1a1a1a", highlightbackground="#333333",
                           highlightthickness=2, padx=14, pady=5)
            box.grid(row=0, column=idx, sticky="ew", padx=7)
            tk.Label(box, text=axis, font=("Arial", 16, "bold"),
                     fg="#cfcfcf", bg="#1a1a1a").pack()
            lbl = tk.Label(box, text="000,000", font=("Courier New", 28, "bold"),
                           fg="white", bg="#1a1a1a")
            lbl.pack()
            self.axis_labels[axis] = lbl

        body = tk.Frame(self, bg=self.bg, padx=18, pady=6)
        body.grid(row=1, column=0, sticky="nsew")
        body.grid_columnconfigure(0, weight=1)
        body.grid_columnconfigure(1, weight=0)
        body.grid_columnconfigure(2, weight=1)
        body.grid_rowconfigure(0, weight=1)

        xy_panel = tk.Frame(body, bg="#151515", highlightbackground="#3a3a3a",
                            highlightthickness=2, padx=12, pady=10)
        xy_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 12))
        tk.Label(xy_panel, text="CONTROLLO X / Y", font=("Arial", 18, "bold"),
                 fg="white", bg="#151515").pack(pady=(0, 6))

        self.xy_size = 430
        self.xy_canvas = tk.Canvas(xy_panel, width=self.xy_size, height=self.xy_size,
                                   bg="#202020", highlightthickness=0)
        self.xy_canvas.pack(anchor="center")
        self.xy_canvas.bind("<ButtonPress-1>", self.xy_press)
        self.xy_canvas.bind("<B1-Motion>", self.xy_motion)
        self.xy_canvas.bind("<ButtonRelease-1>", self.xy_release)
        self.draw_xy()

        xy_info = tk.Frame(xy_panel, bg="#151515")
        xy_info.pack(fill="x", pady=(8, 0))
        self.xy_status = tk.Label(xy_info, text="X: 0  Y: 0 step/s",
                                  font=("Arial", 14, "bold"),
                                  fg="#dddddd", bg="#151515")
        self.xy_status.pack(side="left")
        tk.Button(xy_info, text="STOP XY", command=self.stop_xy,
                  font=("Arial", 13, "bold"), bg="#aa2e25", fg="white",
                  activebackground="#c23c32", activeforeground="white",
                  relief="flat", bd=0, padx=18, pady=7).pack(side="right")

        tk.Frame(body, bg="#101010", width=8).grid(row=0, column=1, sticky="ns")

        z_panel = tk.Frame(body, bg="#151515", highlightbackground="#3a3a3a",
                           highlightthickness=2, padx=12, pady=10)
        z_panel.grid(row=0, column=2, sticky="nsew", padx=(12, 0))
        tk.Label(z_panel, text="CONTROLLO Z", font=("Arial", 18, "bold"),
                 fg="white", bg="#151515").pack(pady=(0, 6))

        self.z_width = 210
        self.z_height = 430
        self.z_canvas = tk.Canvas(z_panel, width=self.z_width, height=self.z_height,
                                  bg="#202020", highlightthickness=0)
        self.z_canvas.pack(anchor="center")
        self.z_canvas.bind("<ButtonPress-1>", self.z_press)
        self.z_canvas.bind("<B1-Motion>", self.z_motion)
        self.z_canvas.bind("<ButtonRelease-1>", self.z_release)
        self.draw_z()

        z_info = tk.Frame(z_panel, bg="#151515")
        z_info.pack(fill="x", pady=(8, 0))
        self.z_status = tk.Label(z_info, text="Z: 0 step/s",
                                 font=("Arial", 14, "bold"),
                                 fg="#dddddd", bg="#151515")
        self.z_status.pack(side="left")
        tk.Button(z_info, text="STOP Z", command=self.stop_z,
                  font=("Arial", 13, "bold"), bg="#aa2e25", fg="white",
                  activebackground="#c23c32", activeforeground="white",
                  relief="flat", bd=0, padx=18, pady=7).pack(side="right")

        footer = tk.Frame(self, bg=self.bg, padx=18, pady=8)
        footer.grid(row=2, column=0, sticky="ew")
        footer.grid_columnconfigure(0, weight=1)
        footer.grid_columnconfigure(1, weight=0)
        footer.grid_columnconfigure(2, weight=1)

        tk.Button(footer, text="INDIETRO", command=self.go_back,
                  font=("Arial", 17, "bold"), bg="#444444", fg="white",
                  activebackground="#666666", activeforeground="white",
                  relief="flat", bd=0, padx=24, pady=9).grid(row=0, column=0, sticky="w")

        tk.Button(footer, text="STOP TUTTO", command=self.stop_all,
                  font=("Arial", 18, "bold"), bg="#aa2e25", fg="white",
                  activebackground="#c23c32", activeforeground="white",
                  relief="flat", bd=0, padx=34, pady=10).grid(row=0, column=1)

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

    def draw_z(self):
        c = self.z_canvas
        w = self.z_width
        h = self.z_height
        c.delete("all")
        margin = 16
        center = h / 2
        c.create_rectangle(w / 2 - 36, margin, w / 2 + 36, h - margin,
                           outline="#707070", width=3)
        c.create_line(w / 2 - 48, center, w / 2 + 48, center, fill="#505050", width=2)
        c.create_text(w / 2, margin + 18, text="+Z", fill="#dddddd", font=("Arial", 13, "bold"))
        c.create_text(w / 2, h - margin - 18, text="-Z", fill="#dddddd", font=("Arial", 13, "bold"))
        usable = (h - 2 * margin) / 2
        py = center - self.z_value * usable
        c.create_rectangle(w / 2 - 52, py - 16, w / 2 + 52, py + 16,
                           fill="#2d7ef7", outline="white", width=2)

    def z_from_event(self, event):
        h = self.z_height
        margin = 16
        center = h / 2
        usable = (h - 2 * margin) / 2
        self.z_value = self.clamp((center - event.y) / usable)
        self.draw_z()
        self.send_current_command()

    def z_press(self, event):
        self.pressed_z = True
        self.z_from_event(event)

    def z_motion(self, event):
        if self.pressed_z:
            self.z_from_event(event)

    def z_release(self, event):
        self.pressed_z = False
        self.z_value = 0.0
        self.draw_z()
        self.send_current_command()

    def stop_xy(self):
        self.xy_x = 0.0
        self.xy_y = 0.0
        self.draw_xy()
        self.send_current_command()

    def stop_z(self):
        self.z_value = 0.0
        self.draw_z()
        self.send_current_command()

    def stop_all(self):
        self.xy_x = 0.0
        self.xy_y = 0.0
        self.z_value = 0.0
        self.draw_xy()
        self.draw_z()
        self.send_current_command()

    def go_back(self):
        self.stop_all()
        self.on_back()

    def send_current_command(self):
        x_sps = self.xy_x * self.XY_MAX_SPS
        y_sps = self.xy_y * self.XY_MAX_SPS
        z_sps = self.z_value * self.Z_MAX_SPS
        self.xy_status.configure(text=f"X: {x_sps:.0f}  Y: {y_sps:.0f} step/s")
        self.z_status.configure(text=f"Z: {z_sps:.0f} step/s")
        self.on_manual_command({"x": x_sps, "y": y_sps, "z": z_sps})

    def refresh_axes(self):
        try:
            values = self.get_axis_values()
            for axis, label in self.axis_labels.items():
                label.configure(text=values.get(axis, "000,000"))
        except Exception:
            pass
        self.after(100, self.refresh_axes)
