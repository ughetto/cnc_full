import math
import tkinter as tk


class ManualeFrame(tk.Frame):
    def __init__(self, master, on_back, get_axis_values, on_manual_command=None, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.configure(bg="#101010")
        self.on_back = on_back
        self.get_axis_values = get_axis_values
        self.on_manual_command = on_manual_command or (lambda data: None)

        self.value_labels = {}
        self.refresh_ms = 100

        self.xy_canvas = None
        self.z_canvas = None

        self.xy_size = 420
        self.xy_margin = 24
        self.xy_knob_radius = 22
        self.xy_center = self.xy_size / 2
        self.xy_max_offset = self.xy_center - self.xy_margin - self.xy_knob_radius

        self.z_width = 120
        self.z_height = 420
        self.z_margin = 24
        self.z_knob_radius = 22
        self.z_center_x = self.z_width / 2
        self.z_center_y = self.z_height / 2
        self.z_max_offset = self.z_center_y - self.z_margin - self.z_knob_radius

        self.xy_knob = None
        self.z_knob = None

        self.xy_dragging = False
        self.z_dragging = False

        self.xy_norm_x = 0.0
        self.xy_norm_y = 0.0
        self.z_norm = 0.0

        self.xy_return_after = None
        self.z_return_after = None

        self.xy_speed_var = tk.StringVar(value="X=+0.0 sps   Y=+0.0 sps")
        self.z_speed_var = tk.StringVar(value="Z=+0.0 sps")

        self.max_speed_sps = 400.0

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

        body = tk.Frame(self, bg="#101010", padx=20, pady=10)
        body.grid(row=1, column=0, sticky="nsew")
        body.grid_columnconfigure(0, weight=1)
        body.grid_columnconfigure(1, weight=0)
        body.grid_rowconfigure(1, weight=1)

        title = tk.Label(
            body,
            text="MANUALE",
            font=("Arial", 28, "bold"),
            fg="#ffffff",
            bg="#101010",
        )
        title.grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 18))

        left_panel = tk.Frame(body, bg="#101010")
        left_panel.grid(row=1, column=0, sticky="nsew", padx=(0, 20))
        left_panel.grid_rowconfigure(0, weight=1)
        left_panel.grid_columnconfigure(0, weight=1)

        self.xy_canvas = tk.Canvas(
            left_panel,
            width=self.xy_size,
            height=self.xy_size,
            bg="#161616",
            highlightthickness=2,
            highlightbackground="#2f2f2f",
            relief="flat",
            bd=0,
        )
        self.xy_canvas.grid(row=0, column=0, sticky="nsew")

        self.draw_xy_pad()
        self.xy_canvas.bind("<Button-1>", self.xy_press)
        self.xy_canvas.bind("<B1-Motion>", self.xy_drag)
        self.xy_canvas.bind("<ButtonRelease-1>", self.xy_release)

        right_panel = tk.Frame(body, bg="#101010")
        right_panel.grid(row=1, column=1, sticky="ns")

        self.z_canvas = tk.Canvas(
            right_panel,
            width=self.z_width,
            height=self.z_height,
            bg="#161616",
            highlightthickness=2,
            highlightbackground="#2f2f2f",
            relief="flat",
            bd=0,
        )
        self.z_canvas.grid(row=0, column=0, sticky="ns")

        self.draw_z_slider()
        self.z_canvas.bind("<Button-1>", self.z_press)
        self.z_canvas.bind("<B1-Motion>", self.z_drag)
        self.z_canvas.bind("<ButtonRelease-1>", self.z_release)

        xy_info = tk.Label(
            body,
            textvariable=self.xy_speed_var,
            font=("Courier New", 20, "bold"),
            fg="#ffffff",
            bg="#101010",
        )
        xy_info.grid(row=2, column=0, sticky="w", pady=(18, 0))

        z_info = tk.Label(
            body,
            textvariable=self.z_speed_var,
            font=("Courier New", 20, "bold"),
            fg="#ffffff",
            bg="#101010",
        )
        z_info.grid(row=2, column=1, sticky="e", pady=(18, 0))

        bottom = tk.Frame(self, bg="#101010", padx=20, pady=16)
        bottom.grid(row=2, column=0, sticky="ew")
        bottom.grid_columnconfigure(0, weight=1)
        bottom.grid_columnconfigure(1, weight=0)

        hint = tk.Label(
            bottom,
            text="Joystick XY e cursore Z con ritorno al centro. Velocità massima: 400 step/s.",
            font=("Arial", 18),
            fg="#cfcfcf",
            bg="#101010",
        )
        hint.grid(row=0, column=0, sticky="w")

        back_button = tk.Button(
            bottom,
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
        back_button.grid(row=0, column=1, sticky="e")

    def draw_xy_pad(self):
        c = self.xy_canvas
        c.delete("all")

        left = self.xy_margin
        top = self.xy_margin
        right = self.xy_size - self.xy_margin
        bottom = self.xy_size - self.xy_margin

        c.create_rectangle(left, top, right, bottom, outline="#4a4a4a", width=2)
        c.create_line(self.xy_center, top, self.xy_center, bottom, fill="#3a3a3a", width=2)
        c.create_line(left, self.xy_center, right, self.xy_center, fill="#3a3a3a", width=2)

        ring_r = 10
        c.create_oval(
            self.xy_center - ring_r, self.xy_center - ring_r,
            self.xy_center + ring_r, self.xy_center + ring_r,
            outline="#9a9a9a", width=2
        )

        self.xy_knob = c.create_oval(
            self.xy_center - self.xy_knob_radius,
            self.xy_center - self.xy_knob_radius,
            self.xy_center + self.xy_knob_radius,
            self.xy_center + self.xy_knob_radius,
            fill="#2d7ef7",
            outline="#8fbaff",
            width=2,
        )

    def draw_z_slider(self):
        c = self.z_canvas
        c.delete("all")

        left = self.z_margin
        top = self.z_margin
        right = self.z_width - self.z_margin
        bottom = self.z_height - self.z_margin

        c.create_rectangle(left, top, right, bottom, outline="#4a4a4a", width=2)
        c.create_line(self.z_center_x, top, self.z_center_x, bottom, fill="#3a3a3a", width=2)
        c.create_line(left, self.z_center_y, right, self.z_center_y, fill="#3a3a3a", width=2)

        ring_r = 10
        c.create_oval(
            self.z_center_x - ring_r, self.z_center_y - ring_r,
            self.z_center_x + ring_r, self.z_center_y + ring_r,
            outline="#9a9a9a", width=2
        )

        self.z_knob = c.create_oval(
            self.z_center_x - self.z_knob_radius,
            self.z_center_y - self.z_knob_radius,
            self.z_center_x + self.z_knob_radius,
            self.z_center_y + self.z_knob_radius,
            fill="#2d7ef7",
            outline="#8fbaff",
            width=2,
        )

    def update_axis_values(self):
        values = self.get_axis_values()
        for axis in ("X", "Y", "Z"):
            self.value_labels[axis].configure(text=values.get(axis, "000,000"))
        self.after(self.refresh_ms, self.update_axis_values)

    def xy_press(self, event):
        self.xy_dragging = True
        if self.xy_return_after is not None:
            self.after_cancel(self.xy_return_after)
            self.xy_return_after = None
        self.update_xy_from_pointer(event.x, event.y)

    def xy_drag(self, event):
        if self.xy_dragging:
            self.update_xy_from_pointer(event.x, event.y)

    def xy_release(self, event):
        self.xy_dragging = False
        self.animate_xy_to_center()

    def z_press(self, event):
        self.z_dragging = True
        if self.z_return_after is not None:
            self.after_cancel(self.z_return_after)
            self.z_return_after = None
        self.update_z_from_pointer(event.y)

    def z_drag(self, event):
        if self.z_dragging:
            self.update_z_from_pointer(event.y)

    def z_release(self, event):
        self.z_dragging = False
        self.animate_z_to_center()

    def update_xy_from_pointer(self, x, y):
        dx = x - self.xy_center
        dy = y - self.xy_center

        distance = math.hypot(dx, dy)
        if distance > self.xy_max_offset and distance > 0:
            scale = self.xy_max_offset / distance
            dx *= scale
            dy *= scale

        self.move_xy_knob(dx, dy)
        self.xy_norm_x = dx / self.xy_max_offset
        self.xy_norm_y = -dy / self.xy_max_offset
        self.publish_manual_command()

    def update_z_from_pointer(self, y):
        dy = y - self.z_center_y
        dy = max(-self.z_max_offset, min(self.z_max_offset, dy))

        self.move_z_knob(dy)
        self.z_norm = -dy / self.z_max_offset
        self.publish_manual_command()

    def move_xy_knob(self, dx, dy):
        cx = self.xy_center + dx
        cy = self.xy_center + dy
        self.xy_canvas.coords(
            self.xy_knob,
            cx - self.xy_knob_radius,
            cy - self.xy_knob_radius,
            cx + self.xy_knob_radius,
            cy + self.xy_knob_radius,
        )

    def move_z_knob(self, dy):
        cy = self.z_center_y + dy
        self.z_canvas.coords(
            self.z_knob,
            self.z_center_x - self.z_knob_radius,
            cy - self.z_knob_radius,
            self.z_center_x + self.z_knob_radius,
            cy + self.z_knob_radius,
        )

    def animate_xy_to_center(self):
        self.xy_norm_x *= 0.68
        self.xy_norm_y *= 0.68

        if abs(self.xy_norm_x) < 0.01:
            self.xy_norm_x = 0.0
        if abs(self.xy_norm_y) < 0.01:
            self.xy_norm_y = 0.0

        dx = self.xy_norm_x * self.xy_max_offset
        dy = -self.xy_norm_y * self.xy_max_offset
        self.move_xy_knob(dx, dy)
        self.publish_manual_command()

        if self.xy_norm_x != 0.0 or self.xy_norm_y != 0.0:
            self.xy_return_after = self.after(16, self.animate_xy_to_center)
        else:
            self.xy_return_after = None

    def animate_z_to_center(self):
        self.z_norm *= 0.68
        if abs(self.z_norm) < 0.01:
            self.z_norm = 0.0

        dy = -self.z_norm * self.z_max_offset
        self.move_z_knob(dy)
        self.publish_manual_command()

        if self.z_norm != 0.0:
            self.z_return_after = self.after(16, self.animate_z_to_center)
        else:
            self.z_return_after = None

    def publish_manual_command(self):
        vx = self.xy_norm_x * self.max_speed_sps
        vy = self.xy_norm_y * self.max_speed_sps
        vz = self.z_norm * self.max_speed_sps

        self.xy_speed_var.set(f"X={vx:+06.1f} sps   Y={vy:+06.1f} sps")
        self.z_speed_var.set(f"Z={vz:+06.1f} sps")

        self.on_manual_command({
            "x": vx,
            "y": vy,
            "z": vz,
        })
