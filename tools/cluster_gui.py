#!/usr/bin/env python3
"""Window version of cluster_test.py: drive the Mazda 3 cluster bridge by hand.

Sliders for the gauges, switches for the lamps and gear, a box for `#`
commands (#stat, #scan, #sweep, custom frames...) and the board's output in
one window, all on the one COM port. Close the Arduino Serial Monitor first.

Requires pyserial:  pip install pyserial
Run:                python cluster_gui.py
"""

import math
import queue
import sys
import threading
import time
try:
    import tkinter as tk
    from tkinter import ttk
except ImportError:
    sys.exit("This Python was installed without tkinter (the window library).\n"
             "Windows: run the python.org installer again, choose Modify, and tick\n"
             "'tcl/tk and IDLE'. Linux: install python3-tk.")

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")

from cluster_test import LAMP_KEYS, packet

SEND_PERIOD_MS = 25  # 40 packets/s, well inside the board's 1 s timeout

LAMP_NAMES = {
    "E": "Check engine",
    "B": "Battery",
    "O": "Oil pressure",
    "L": "Left turn",
    "Y": "Right turn",
    "H": "High beam",
    "P": "Parking brake",
}

GEARS = [("None", None), ("P", 99), ("R", -1), ("N", 0), ("D", 1)]  # 99 = park

# One-click commands. Each is sent as "#<text>\n".
QUICK_COMMANDS = [
    ("Bus health", "stat"),
    ("Watch on/off", "watch"),
    ("List frames", "?"),
    ("Clear all", "-"),
    ("Stop scan", "scan"),
    ("Resume scan", "scan resume"),
    ("Stop sweep", "sweep"),
]


class ClusterGui:
    def __init__(self, root):
        self.root = root
        self.port = None
        self.reader = None
        self.rx = queue.Queue()
        self.start = time.time()

        root.title("Mazda 3 cluster test")
        root.columnconfigure(0, weight=1)
        root.rowconfigure(3, weight=1)

        self._build_connection(root)
        self._build_gauges(root)
        self._build_lamps(root)
        self._build_console(root)

        root.after(SEND_PERIOD_MS, self._tick)
        root.protocol("WM_DELETE_WINDOW", self._close)

    # ------------------------------------------------------------- layout
    def _build_connection(self, root):
        bar = ttk.Frame(root, padding=6)
        bar.grid(row=0, column=0, sticky="ew")
        ttk.Label(bar, text="Port").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(bar, textvariable=self.port_var, width=16)
        self.port_box.pack(side="left", padx=4)
        ttk.Button(bar, text="Refresh", command=self._refresh_ports).pack(side="left")
        self.connect_btn = ttk.Button(bar, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=4)
        self.status = ttk.Label(bar, text="not connected")
        self.status.pack(side="left", padx=8)
        self._refresh_ports()

    def _build_gauges(self, root):
        box = ttk.LabelFrame(root, text="Gauges", padding=6)
        box.grid(row=1, column=0, sticky="ew", padx=6)
        box.columnconfigure(1, weight=1)
        self.rpm = self._slider(box, 0, "RPM", 0, 8000, 800)
        self.speed = self._slider(box, 1, "Speed (mph)", 0, 140, 0)
        self.coolant = self._slider(box, 2, "Coolant (°C)", 40, 130, 90)
        self.fuel = self._slider(box, 3, "Fuel (%)", 0, 100, 75)
        self.throttle = self._slider(box, 4, "Throttle (%)", 0, 100, 0)
        self.sweep = tk.BooleanVar()
        ttk.Checkbutton(box, text="Sweep RPM and speed up and down", variable=self.sweep).grid(
            row=5, column=0, columnspan=3, sticky="w", pady=(4, 0))

    def _slider(self, parent, row, label, lo, hi, value):
        var = tk.DoubleVar(value=value)
        ttk.Label(parent, text=label, width=13).grid(row=row, column=0, sticky="w")
        ttk.Scale(parent, from_=lo, to=hi, variable=var).grid(row=row, column=1, sticky="ew", padx=4)
        shown = ttk.Label(parent, width=6, anchor="e")
        shown.grid(row=row, column=2)
        var.trace_add("write", lambda *_: shown.config(text=f"{var.get():.0f}"))
        shown.config(text=f"{value:.0f}")
        return var

    def _build_lamps(self, root):
        row = ttk.Frame(root, padding=(6, 4))
        row.grid(row=2, column=0, sticky="ew")

        lamps = ttk.LabelFrame(row, text="Lamps", padding=6)
        lamps.pack(side="left", fill="y")
        self.lamps = {}
        for i, key in enumerate(LAMP_KEYS):
            var = tk.BooleanVar()
            ttk.Checkbutton(lamps, text=LAMP_NAMES.get(key, key), variable=var).grid(
                row=i // 2, column=i % 2, sticky="w", padx=(0, 10))
            self.lamps[key] = var
        hazard = ttk.Button(lamps, text="Hazards", command=self._hazards)
        hazard.grid(row=(len(LAMP_KEYS) + 1) // 2, column=0, sticky="w", pady=(4, 0))

        gear = ttk.LabelFrame(row, text="Gear", padding=6)
        gear.pack(side="left", fill="y", padx=6)
        self.gear = tk.StringVar(value="None")
        for name, _ in GEARS:
            ttk.Radiobutton(gear, text=name, value=name, variable=self.gear).pack(anchor="w")

        quick = ttk.LabelFrame(row, text="Commands", padding=6)
        quick.pack(side="left", fill="both", expand=True)
        for i, (name, cmd) in enumerate(QUICK_COMMANDS):
            ttk.Button(quick, text=name, command=lambda c=cmd: self.send_command(c)).grid(
                row=i // 4, column=i % 4, sticky="ew", padx=2, pady=2)

    def _build_console(self, root):
        box = ttk.LabelFrame(root, text="Board output", padding=6)
        box.grid(row=3, column=0, sticky="nsew", padx=6, pady=(0, 6))
        box.columnconfigure(0, weight=1)
        box.rowconfigure(0, weight=1)
        self.log = tk.Text(box, height=14, wrap="none", state="disabled",
                           font=("Consolas", 10) if sys.platform == "win32" else ("Courier", 10))
        self.log.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(box, command=self.log.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.log.config(yscrollcommand=scroll.set)

        entry_row = ttk.Frame(box)
        entry_row.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(4, 0))
        ttk.Label(entry_row, text="#").pack(side="left")
        self.cmd = tk.StringVar()
        entry = ttk.Entry(entry_row, textvariable=self.cmd)
        entry.pack(side="left", fill="x", expand=True, padx=4)
        entry.bind("<Return>", lambda _: self._send_typed())
        ttk.Button(entry_row, text="Send", command=self._send_typed).pack(side="left")
        ttk.Button(entry_row, text="Clear log", command=self._clear_log).pack(side="left", padx=(4, 0))
        ttk.Label(box, text="e.g.  scan h 0 00   ·   sweep h 212 0   ·   212 00 00 00 00 40 00 00 00",
                  foreground="gray").grid(row=2, column=0, columnspan=2, sticky="w")

    # ------------------------------------------------------------ serial
    def _refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.port:
            self._disconnect()
            return
        name = self.port_var.get().strip()
        if not name:
            self._append("pick a COM port first\n")
            return
        try:
            self.port = serial.serial_for_url(name, baudrate=115200, timeout=0.1)
        except serial.SerialException as e:
            self._append(f"couldn't open {name}: {e}\n(is the Serial Monitor or SimHub holding it?)\n")
            return
        self.connect_btn.config(text="Disconnect")
        self.status.config(text="connected (board restarting...)")
        self.reader = threading.Thread(target=self._read_loop, args=(self.port,), daemon=True)
        self.reader.start()

    def _disconnect(self):
        port, self.port = self.port, None
        try:
            port.close()
        except serial.SerialException:
            pass
        self.connect_btn.config(text="Connect")
        self.status.config(text="not connected")

    def _read_loop(self, port):
        while self.port is port:
            try:
                data = port.read(256)
            except (serial.SerialException, OSError, TypeError):
                break
            if data:
                self.rx.put(data.decode("ascii", errors="replace").replace("\r", ""))

    def _write(self, data):
        if not self.port:
            return
        try:
            self.port.write(data)
        except serial.SerialException as e:
            self._append(f"write failed: {e}\n")
            self._disconnect()

    def send_command(self, text):
        if not self.port:
            self._append("not connected\n")
            return
        self._append(f"> #{text}\n")
        self._write(f"#{text}\n".encode("ascii", errors="ignore"))

    def _send_typed(self):
        text = self.cmd.get().strip().lstrip("#")
        if text:
            self.send_command(text)
            self.cmd.set("")

    # -------------------------------------------------------------- loop
    def _tick(self):
        while not self.rx.empty():
            self._append(self.rx.get_nowait())
        if self.port:
            rpm, speed = self.rpm.get(), self.speed.get()
            if self.sweep.get():
                t = time.time() - self.start
                k = (1 - math.cos(t * 2 * math.pi / 8)) / 2  # 8 s full cycle
                rpm, speed = k * 8000, k * 140
            lamps = {k: v.get() for k, v in self.lamps.items()}
            gear = dict(GEARS)[self.gear.get()]
            self._write(packet(rpm, speed, self.coolant.get(), self.throttle.get(),
                               self.fuel.get(), lamps, gear))
        self.root.after(SEND_PERIOD_MS, self._tick)

    def _hazards(self):
        on = not (self.lamps["L"].get() and self.lamps["Y"].get())
        self.lamps["L"].set(on)
        self.lamps["Y"].set(on)

    def _append(self, text):
        self.log.config(state="normal")
        self.log.insert("end", text)
        self.log.see("end")
        self.log.config(state="disabled")

    def _clear_log(self):
        self.log.config(state="normal")
        self.log.delete("1.0", "end")
        self.log.config(state="disabled")

    def _close(self):
        if self.port:
            self._disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    ClusterGui(root)
    root.minsize(640, 560)
    root.mainloop()


if __name__ == "__main__":
    main()
