import threading
import tkinter as tk

try:
    import serial
except Exception:
    serial = None

from manuale_v4 import ManualeFrame


class DROApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("DEBUG TX JOG")
        self.configure(bg="#101010")
        self.attributes("-fullscreen", True)

        self.positions = {"X": "000,000", "Y": "000,000", "Z": "000,000"}
        self.serial_port = "/dev/serial0"
        self.baudrate = 115200
        self.serial_conn = None
        self.serial_lock = threading.Lock()

        self.start_serial()

        self.frame = ManualeFrame(
            self,
            on_back=self.destroy,
            get_axis_values=lambda: self.positions,
            on_manual_command=self.send_manual_command,
            bg="#101010",
        )
        self.frame.pack(fill="both", expand=True)

    def start_serial(self):
        if serial is None:
            print("pyserial non disponibile")
            return
        try:
            self.serial_conn = serial.Serial(self.serial_port, self.baudrate, timeout=0.05)
            print("Seriale aperta:", self.serial_port, self.baudrate)
        except Exception as e:
            print("Errore apertura seriale:", e)
            self.serial_conn = None

    def send_manual_command(self, data):
        cmd = {
            "x": float(data.get("x", 0.0)),
            "y": float(data.get("y", 0.0)),
            "z": float(data.get("z", 0.0)),
        }
        line = f'JOG,X:{cmd["x"]:.1f},Y:{cmd["y"]:.1f},Z:{cmd["z"]:.1f}\n'
        print("TX>", line.strip())
        if self.serial_conn is not None:
            try:
                with self.serial_lock:
                    self.serial_conn.write(line.encode("utf-8"))
                    self.serial_conn.flush()
            except Exception as e:
                print("WRITE ERR:", e)


if __name__ == "__main__":
    DROApp().mainloop()
