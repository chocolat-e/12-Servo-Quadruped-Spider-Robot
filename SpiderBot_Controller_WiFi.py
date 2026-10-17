import socket
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
ROBOT_IP = "192.168.4.1"
ROBOT_PORT = 5000
DISCOVERY_SSID = "SpiderBot"
class SpiderBotController:
    def __init__(self, root):
        self.root = root
        self.root.title("SpiderBot Controller - WiFi")
        self.root.geometry("780x570")
        self.root.minsize(720, 520)
        self.sock = None
        self.connected = False
        self.running = True
        self.keys_down = set()
        self.status_var = tk.StringVar(
            value="Connect to WiFi: SpiderBot"
        )
        self.build_ui()
        self.root.bind("<KeyPress>", self.on_key_press)
        self.root.bind("<KeyRelease>", self.on_key_release)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        # Try the robot automatically.
        self.root.after(300, self.auto_connect)
    # ---------------------------------------------------------
    # UI
    # ---------------------------------------------------------
    def build_ui(self):
        top = ttk.Frame(self.root, padding=12)
        top.pack(fill="x")
        ttk.Label(
            top,
            text="SPIDERBOT",
            font=("Segoe UI", 18, "bold")
        ).pack(side="left")
        ttk.Label(
            top,
            textvariable=self.status_var
        ).pack(side="right")
        info = ttk.LabelFrame(
            self.root,
            text="WiFi",
            padding=10
        )
        info.pack(fill="x", padx=12, pady=(0, 10))
        ttk.Label(
            info,
            text="1. Connect Windows WiFi to  SpiderBot\n"
                 "2. Press Connect\n"
                 "3. Control with buttons or keyboard"
        ).pack(side="left")
        ttk.Button(
            info,
            text="Connect",
            command=self.auto_connect
        ).pack(side="right", padx=5)
        ttk.Button(
            info,
            text="Disconnect",
            command=self.disconnect
        ).pack(side="right", padx=5)
        movement = ttk.LabelFrame(
            self.root,
            text="Movement",
            padding=12
        )
        movement.pack(
            fill="both",
            expand=True,
            padx=12,
            pady=(0, 10)
        )
        pad = ttk.Frame(movement)
        pad.pack(side="left", padx=25)
        self.button(pad, "Forward", "FORWARD", 0, 1)
        self.button(pad, "Turn left", "LEFT", 1, 0)
        self.button(pad, "STOP", "STOP", 1, 1)
        self.button(pad, "Turn right", "RIGHT", 1, 2)
        self.button(pad, "Back", "BACK", 2, 1)
        rotate = ttk.Frame(movement)
        rotate.pack(side="left", padx=30)
        ttk.Label(
            rotate,
            text="Rotate in place",
            font=("Segoe UI", 10, "bold")
        ).pack(pady=(0, 8))
        row = ttk.Frame(rotate)
        row.pack()
        self.button(row, "Left", "ROTATE_LEFT", 0, 0)
        self.button(row, "Right", "ROTATE_RIGHT", 0, 1)
        ttk.Label(
            rotate,
            text="W  Forward\n"
                 "S  Back\n"
                 "A / D  Turn\n"
                 "Q / E  Rotate\n"
                 "Space  Stop",
            justify="left"
        ).pack(anchor="w", pady=20)
        actions = ttk.LabelFrame(
            self.root,
            text="Actions",
            padding=10
        )
        actions.pack(
            fill="x",
            padx=12,
            pady=(0, 10)
        )
        for text, command in [
            ("Stand", "STAND"),
            ("Sit", "SIT"),
            ("Home", "HOME"),
            ("Wave left", "WAVE_LEFT"),
            ("Wave right", "WAVE_RIGHT"),
        ]:
            ttk.Button(
                actions,
                text=text,
                command=lambda c=command: self.send(c)
            ).pack(side="left", padx=5)
        log_frame = ttk.LabelFrame(
            self.root,
            text="Robot",
            padding=8
        )
        log_frame.pack(
            fill="both",
            padx=12,
            pady=(0, 12)
        )
        self.log = tk.Text(
            log_frame,
            height=6,
            state="disabled",
            font=("Consolas", 9)
        )
        self.log.pack(fill="both", expand=True)
    def button(self, parent, text, command, row, column):
        ttk.Button(
            parent,
            text=text,
            width=14,
            command=lambda c=command: self.send(c)
        ).grid(
            row=row,
            column=column,
            padx=5,
            pady=5
        )
    # ---------------------------------------------------------
    # Logging
    # ---------------------------------------------------------
    def log_message(self, message):
        def update():
            self.log.configure(state="normal")
            self.log.insert("end", message + "\n")
            self.log.see("end")
            self.log.configure(state="disabled")
        self.root.after(0, update)
    def set_status(self, message):
        self.root.after(
            0,
            lambda: self.status_var.set(message)
        )
    # ---------------------------------------------------------
    # WiFi / TCP
    # ---------------------------------------------------------
    def auto_connect(self):
        # We will loop connection attempt to solve the reconnect issue
        if self.connected:
            return
        self.set_status("Connecting to 192.168.4.1...")
        self.reconnect_flag = True # Set flag to start looping
        threading.Thread(
            target=self.connect_worker,
            daemon=True
        ).start()
    def reconnect(self):
        # Function to continuously try reconnecting while the reconnect_flag is True
        while self.reconnect_flag and not self.connected:
             try:
                 sock = socket.socket(
                     socket.AF_INET,
                     socket.SOCK_STREAM
                 )
                 sock.settimeout(3.0)
                 sock.connect((ROBOT_IP, ROBOT_PORT))
                 self.sock = sock
                 self.connected = True
                 self.set_status("Connected - SpiderBot")
                 self.log_message(
                     f"Connected to {ROBOT_IP}:{ROBOT_PORT}"
                 )
                 self.send("PING")
                 threading.Thread(
                     target=self.reader_loop,
                     daemon=True
                 ).start()
                 # Break the loop if connected
                 break
             except Exception as exc:
                 self.set_status(
                     "Not connected - retrying..."
                 )
                 self.log_message(
                     f"Connection failed, retrying..."
                 )
                 time.sleep(2) # Wait 2 seconds before next attempt
    def connect_worker(self):
         # Wrap connect_worker to call the reconnect loop
         self.reconnect()
    def reader_loop(self):
        buffer = b""
        while self.running and self.connected:
            try:
                data = self.sock.recv(1024)
                if not data:
                    break
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(
                        b"\n", 1
                    )
                    message = line.decode(
                        "utf-8",
                        errors="ignore"
                    ).strip()
                    if message:
                        self.log_message(
                            "< " + message
                        )
            except Exception:
                break
        self.connected = False
        self.set_status("Disconnected")
        # When connection drops and running is true, try to reconnect
        if self.running and self.reconnect_flag:
           self.set_status("Connection lost - reconnecting...")
           threading.Thread(target=self.connect_worker, daemon=True).start()
    def disconnect(self):
        # Disable auto reconnect loop when disconnect is clicked
        self.reconnect_flag = False
        self.connected = False
        if self.sock is not None:
            try:
                self.sock.close()
            except Exception:
                pass
        self.sock = None
        self.set_status("Disconnected")
    def send(self, command):
        if not self.connected or self.sock is None:
            self.log_message(
                "Not connected."
            )
            return
        try:
            self.sock.sendall(
                (command + "\n").encode("ascii")
            )
            self.log_message(
                "> " + command
            )
        except Exception as exc:
            self.log_message(
                f"Send error: {exc}"
            )
            self.disconnect()
    # ---------------------------------------------------------
    # Keyboard
    # ---------------------------------------------------------
    def on_key_press(self, event):
        key = event.keysym.lower()
        commands = {
            "w": "FORWARD",
            "s": "BACK",
            "a": "LEFT",
            "d": "RIGHT",
            "q": "ROTATE_LEFT",
            "e": "ROTATE_RIGHT",
            "space": "STOP",
        }
        if key not in commands:
            return
        if key in self.keys_down:
            return
        self.keys_down.add(key)
        self.send(commands[key])
    def on_key_release(self, event):
        self.keys_down.discard(
            event.keysym.lower()
        )
    # ---------------------------------------------------------
    # Shutdown
    # ---------------------------------------------------------
    def close(self):
        self.running = False
        self.reconnect_flag = False
        if self.connected:
            try:
                self.send("STOP")
                time.sleep(0.1)
            except Exception:
                pass
        self.disconnect()
        self.root.destroy()
if __name__ == "__main__":
    root = tk.Tk()
    app = SpiderBotController(root)
    root.mainloop()

