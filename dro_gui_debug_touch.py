import tkinter as tk
from manuale_v5 import ManualeFrame

root = tk.Tk()
root.attributes("-fullscreen", True)
frame = ManualeFrame(root, on_back=root.destroy,
                     get_axis_values=lambda: {"X": "000,000", "Y": "000,000", "Z": "000,000"},
                     on_manual_command=lambda data: print("CMD", data),
                     bg="#101010")
frame.pack(fill="both", expand=True)
root.mainloop()
