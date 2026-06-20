import tkinter as tk
import threading

try:
    import serial
except Exception:
    serial = None


class ForceJogApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("FORCE JOG TEST")
        self.configure(bg="#101010")
        self.attributes("-fullscreen", True)

        self.serial_port = "/dev/serial0"
        self.baudrate = 115200
        self.ser = None
        self.lock = threading.Lock()

        self.status_var = tk.StringVar(value="Seriale non aperta")
        self.open_serial()

        root = tk.Frame(self, bg="#101010", padx=30, pady=30)
        root.pack(fill="both", expand=True)

        tk.Label(root, text="TEST FORZATO JOG", font=("Arial", 28, "bold"),
                 fg="white", bg="#101010").pack(pady=(0, 20))

        tk.Label(root, textvariable=self.status_var, font=("Courier New", 18),
                 fg="#9fd0ff", bg="#101010").pack(pady=(0, 20))

        grid = tk.Frame(root, bg="#101010")
        grid.pack()

        buttons = [
            ("X +100", lambda: self.send_jog(100, 0, 0)),
            ("X -100", lambda: self.send_jog(-100, 0, 0)),
            ("Y +100", lambda: self.send_jog(0, 100, 0)),
            ("Y -100", lambda: self.send_jog(0, -100, 0)),
            ("Z +100", lambda: self.send_jog(0, 0, 100)),
            ("Z -100", lambda: self.send_jog(0, 0, -100)),
            ("STOP", lambda: self.send_jog(0, 0, 0)),
        ]

        for i, (txt, cmd) in enumerate(buttons):
            tk.Button(grid, text=txt, command=cmd, font=("Arial", 22, "bold"),
                      bg="#2d7ef7" if txt != "STOP" else "#aa2e25",
                      fg="white", activeforeground="white",
                      activebackground="#4d96ff" if txt != "STOP" else "#c23c32",
                      relief="flat", bd=0, padx=20, pady=18, width=12).grid(
                row=i//2, column=i%2, padx=12, pady=12
            )

        tk.Button(root, text="ESCI", command=self.destroy, font=("Arial", 18, "bold"),
                  bg="#444444", fg="white", relief="flat", bd=0,
                  padx=18, pady=10).pack(pady=(30, 0))

    def open_serial(self):
        if serial is None:
            self.status_var.set("pyserial non disponibile")
            return
        try:
            self.ser = serial.Serial(self.serial_port, self.baudrate, timeout=0.1)
            self.status_var.set(f"Aperta {self.serial_port} @ {self.baudrate}")
            print("Seriale aperta:", self.serial_port, self.baudrate)
        except Exception as e:
            self.status_var.set(f"Errore seriale: {e}")
            print("Errore seriale:", e)

    def send_jog(self, x, y, z):
        line = f"JOG,X:{x:.1f},Y:{y:.1f},Z:{z:.1f}\n"
        print("TX>", line.strip())
        self.status_var.set(line.strip())
        if self.ser is None:
            return
        try:
            with self.lock:
                self.ser.write(line.encode("utf-8"))
                self.ser.flush()
        except Exception as e:
            print("WRITE ERR:", e)
            self.status_var.set(f"WRITE ERR: {e}")


if __name__ == "__main__":
    ForceJogApp().mainloop()
