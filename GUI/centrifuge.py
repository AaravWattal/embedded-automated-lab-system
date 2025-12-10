import socket
import threading
import queue
import tkinter as tk
from tkinter import ttk, messagebox

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

# ==============================
# Configuration
# ==============================
DEFAULT_HOST = "192.168.1.49"
DEFAULT_PORT = 5000


# ==============================
# Networking client (background thread)
# ==============================
class HotplateClient(threading.Thread):
    """
    Handles TCP communication with the hot plate.
    - Connects to host:port
    - Reads lines and pushes them into a queue
    - Exposes a send_setpoint() method for the GUI to send integers
    """

    def __init__(self, host, port, incoming_queue, status_queue):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.incoming_queue = incoming_queue   # status lines from device
        self.status_queue = status_queue       # connection/status messages for GUI
        self.stop_event = threading.Event()
        self.sock = None
        self.sock_file = None

    def run(self):
        try:
            self.status_queue.put(f"Connecting to {self.host}:{self.port} ...")
            self.sock = socket.create_connection((self.host, self.port), timeout=5.0)
            # text-mode file wrapper for line-based reading
            self.sock_file = self.sock.makefile("r", encoding="utf-8", newline="\n")

            self.status_queue.put("Connected.")

            # Read lines until stopped or connection closes
            for raw_line in self.sock_file:
                if self.stop_event.is_set():
                    break
                # Clean up weird \n\r etc.
                line = raw_line.strip()
                if line:
                    self.incoming_queue.put(line)
        except Exception as e:
            self.status_queue.put(f"Connection error: {e}")
        finally:
            self.close()
            self.status_queue.put("Disconnected.")

    def send_setpoint(self, value: int):
        """
        Send an integer setpoint followed by newline, e.g. "50\n".
        """
        try:
            if self.sock is None:
                self.status_queue.put("Not connected.")
                return
            msg = f"{value}\n"
            self.sock.sendall(msg.encode("utf-8"))
            self.status_queue.put(f"Sent setpoint: {value}")
        except Exception as e:
            self.status_queue.put(f"Send error: {e}")

    def close(self):
        self.stop_event.set()
        try:
            if self.sock_file is not None:
                self.sock_file.close()
        except Exception:
            pass
        try:
            if self.sock is not None:
                self.sock.close()
        except Exception:
            pass


# ==============================
# GUI
# ==============================
class CentrifugeGUI:
    def __init__(self, master):
        self.master = master;
        self.master.title("Centrifuge Controller")

        # Queues for communication between GUI and network thread
        self.incoming_queue = queue.Queue()  # status lines from device
        self.status_queue = queue.Queue()    # connection / log messages

        self.client = None  # HotplateClient instance

        # For slider/SP logic
        self.current_sp_value = None          # numeric SP from device / last set
        self.slider_revert_after_id = None    # after() id for revert timer
        self.user_adjusting_slider = False    # True while user is fiddling with slider
        self.ignore_slider_change = False     # Used to ignore callbacks on programmatic slider moves

        # For plotting
        self.temp_history = []
        self.sp_history = []
        self.sample_indices = []
        self.sample_count = 0
        self.max_points = 300  # keep last N samples

        self._build_widgets()
        self._poll_queues()

        # Clean shutdown on window close
        self.master.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_widgets(self):
        # Main frame
        main = ttk.Frame(self.master, padding=12)
        main.grid(row=0, column=0, sticky="nsew")

        self.master.rowconfigure(0, weight=1)
        self.master.columnconfigure(0, weight=1)

        # Connection controls
        conn_frame = ttk.LabelFrame(main, text="Connection", padding=8)
        conn_frame.grid(row=0, column=0, sticky="ew")

        ttk.Label(conn_frame, text="Host:").grid(row=0, column=0, sticky="w")
        self.host_var = tk.StringVar(value=DEFAULT_HOST)
        host_entry = ttk.Entry(conn_frame, textvariable=self.host_var, width=15)
        host_entry.grid(row=0, column=1, padx=(0, 8))

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=2, sticky="w")
        self.port_var = tk.StringVar(value=str(DEFAULT_PORT))
        port_entry = ttk.Entry(conn_frame, textvariable=self.port_var, width=6)
        port_entry.grid(row=0, column=3, padx=(0, 8))

        self.connect_button = ttk.Button(conn_frame, text="Connect", command=self.on_connect)
        self.connect_button.grid(row=0, column=4, padx=(0, 4))

        self.disconnect_button = ttk.Button(conn_frame, text="Disconnect", command=self.on_disconnect, state="disabled")
        self.disconnect_button.grid(row=0, column=5)

        # Status display (RPM / SP / State)
        status_frame = ttk.LabelFrame(main, text="Centrifuge Status", padding=8)
        status_frame.grid(row=1, column=0, sticky="ew", pady=(8, 0))

        status_frame.columnconfigure(1, weight=1)

        ttk.Label(status_frame, text="RPM:").grid(row=0, column=0, sticky="w")
        self.temp_var = tk.StringVar(value="--.- RPM")
        ttk.Label(status_frame, textvariable=self.temp_var, font=("Helvetica", 14, "bold")).grid(
            row=0, column=1, sticky="w"
        )

        ttk.Label(status_frame, text="Setpoint (SP):").grid(row=1, column=0, sticky="w")
        self.sp_var = tk.StringVar(value="--.- RPM")
        ttk.Label(status_frame, textvariable=self.sp_var, font=("Helvetica", 14, "bold")).grid(
            row=1, column=1, sticky="w"
        )

        ttk.Label(status_frame, text="State:").grid(row=2, column=0, sticky="w")
        self.state_var = tk.StringVar(value="--")
        ttk.Label(status_frame, textvariable=self.state_var).grid(row=2, column=1, sticky="w")

        # Setpoint control (slider + Set button)
        control_frame = ttk.LabelFrame(main, text="Setpoint Control", padding=8)
        control_frame.grid(row=2, column=0, sticky="ew", pady=(8, 0))

        # Dynamic label that shows the currently chosen SP
        self.sp_slider_label_var = tk.StringVar(value="SP to set: -- RPM")
        ttk.Label(control_frame, textvariable=self.sp_slider_label_var).grid(
            row=0, column=0, columnspan=4, sticky="w"
        )

        # Slider from 0 to 250RPM
        self.sp_slider_var = tk.IntVar(value=25)

        # Left endpoint label "20"
        ttk.Label(control_frame, text="0").grid(row=1, column=0, sticky="w")

        self.sp_slider = ttk.Scale(
            control_frame,
            from_=0,
            to=250,
            orient="horizontal",
            variable=self.sp_slider_var,
            command=self.on_slider_changed,  # called whenever slider moves
        )
        self.sp_slider.grid(row=1, column=1, padx=(4, 4), sticky="ew")
        control_frame.columnconfigure(1, weight=1)

        # Right endpoint label "70"
        ttk.Label(control_frame, text="250").grid(row=1, column=2, sticky="e")

        self.set_button = ttk.Button(control_frame, text="Set", command=self.on_set_sp, state="disabled")
        self.set_button.grid(row=1, column=3, padx=(8, 0))

        # Initialize label with slider's starting value
        self.update_slider_label(self.sp_slider_var.get())

        # Plot frame (replaces logs)
        plot_frame = ttk.LabelFrame(main, text="RPM / SP History", padding=8)
        plot_frame.grid(row=3, column=0, sticky="nsew", pady=(8, 0))
        main.rowconfigure(3, weight=1)
        plot_frame.rowconfigure(0, weight=1)
        plot_frame.columnconfigure(0, weight=1)

        self.fig = Figure(figsize=(5, 3), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_xlabel("Sample")
        self.ax.set_ylabel("RPM")
        self.ax.set_ylim(20, 70)

        # Two lines: Temp (blue) and SP (orange)
        self.temp_line, = self.ax.plot([], [], label="RPM", color="tab:blue")
        self.sp_line,   = self.ax.plot([], [], label="SP",   color="tab:orange")
        self.ax.legend(loc="upper right")

        self.canvas = FigureCanvasTkAgg(self.fig, master=plot_frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

        # Status bar
        self.status_var = tk.StringVar(value="Not connected.")
        status_bar = ttk.Label(self.master, textvariable=self.status_var, anchor="w", padding=(8, 4))
        status_bar.grid(row=1, column=0, sticky="ew")

    # ==============================
    # Connection handlers
    # ==============================
    def on_connect(self):
        if self.client is not None:
            messagebox.showinfo("Already connected", "A connection is already active.")
            return

        host = self.host_var.get().strip()
        port_str = self.port_var.get().strip()

        try:
            port = int(port_str)
        except ValueError:
            messagebox.showerror("Invalid port", "Port must be an integer.")
            return

        self.client = HotplateClient(
            host=host,
            port=port,
            incoming_queue=self.incoming_queue,
            status_queue=self.status_queue,
        )
        self.client.start()

        self.connect_button.config(state="disabled")
        self.disconnect_button.config(state="normal")
        self.set_button.config(state="normal")

    def on_disconnect(self):
        if self.client is not None:
            self.client.close()
            self.client = None
        self.connect_button.config(state="normal")
        self.disconnect_button.config(state="disabled")
        self.set_button.config(state="disabled")
        self.status_var.set("Disconnected.")

    # ==============================
    # Slider label helper
    # ==============================
    def update_slider_label(self, value):
        """Update the 'SP to set: X °C' label."""
        try:
            v = float(value)
        except (TypeError, ValueError):
            self.sp_slider_label_var.set("SP to set: -- RPM")
            return
        self.sp_slider_label_var.set(f"SP to set: {v:.1f} RPM")

    # ==============================
    # Slider + Setpoint handlers
    # ==============================
    def on_slider_changed(self, value_str=None):
        """
        Called whenever the slider moves.
        Start/reset a 5s timer; if user doesn't press Set, we revert.
        """
        if self.ignore_slider_change:
            # Programmatic move: don't treat as user adjustment
            return

        # Update label with current slider value
        self.update_slider_label(value_str)

        # Mark that user is actively adjusting
        self.user_adjusting_slider = True

        # Reset existing timer if any
        if self.slider_revert_after_id is not None:
            self.master.after_cancel(self.slider_revert_after_id)

        # Schedule revert in 5 seconds
        self.slider_revert_after_id = self.master.after(5000, self.revert_slider_to_current_sp)

    def revert_slider_to_current_sp(self):
        """
        Called when user moved slider but didn't press Set within 5s.
        Revert slider back to the *current* SP.
        """
        self.slider_revert_after_id = None
        self.user_adjusting_slider = False

        if self.current_sp_value is not None:
            self.ignore_slider_change = True
            self.sp_slider_var.set(int(round(self.current_sp_value)))
            self.ignore_slider_change = False
            self.update_slider_label(self.current_sp_value)

    def on_set_sp(self):
        if self.client is None:
            messagebox.showwarning("Not connected", "Connect to the centrifuge first.")
            return

        # Slider is already constrained to [20, 70]
        value = int(round(self.sp_slider_var.get()))

        self.client.send_setpoint(value)

        # Optimistically treat this as the new SP
        self.current_sp_value = value
        self.sp_var.set(f"{value:.1f} C")
        self.update_slider_label(value)

        # User is done adjusting
        self.user_adjusting_slider = False

        # Cancel revert timer if any
        if self.slider_revert_after_id is not None:
            self.master.after_cancel(self.slider_revert_after_id)
            self.slider_revert_after_id = None

    # ==============================
    # Plot updating
    # ==============================
    def _update_plot(self):
        if not self.sample_indices:
            return

        self.temp_line.set_data(self.sample_indices, self.temp_history)
        self.sp_line.set_data(self.sample_indices, self.sp_history)

        # X axis from first to last sample (or at least 20)
        xmin = min(self.sample_indices)
        xmax = max(self.sample_indices)
        self.ax.set_xlim(max(1, xmin), max(20, xmax))

        # Y axis fixed 0-250 as requested
        self.ax.set_ylim(0, 250)

        self.canvas.draw_idle()

    # ==============================
    # Queue polling & parsing
    # ==============================
    def _poll_queues(self):
        # Process status (connection/log) messages
        while True:
            try:
                msg = self.status_queue.get_nowait()
            except queue.Empty:
                break
            else:
                self._append_log(msg + "\n")
                self.status_var.set(msg)

        # Process incoming lines from device
        while True:
            try:
                line = self.incoming_queue.get_nowait()
            except queue.Empty:
                break
            else:
                self._handle_device_line(line)

        # Schedule next poll
        self.master.after(100, self._poll_queues)

    def _append_log(self, text):
        # No GUI log now; just print to console if you want debugging
        print(text, end="")

    def _handle_device_line(self, line: str):
        """
        Expected format:
        RPM: %.2f | SP: %.1f | STATE: %s
        We'll parse it and update labels/plot; if parsing fails, we just ignore it.
        """
        self._append_log(line + "\n")

        # Clean up whitespace (in case of \r, etc.)
        line = line.strip()

        if not line.startswith("RPM:"):
            return

        try:
            # Example: "RPM: 23.45 | SP: 50.0 | STATE: 1"
            parts = [p.strip() for p in line.split("|")]
            if len(parts) < 3:
                return

            temp_part = parts[0]  # "Temp: 23.45 C"
            sp_part = parts[1]  # "SP: 50.0 C"
            state_part = parts[2]  # "STATE: 1" or "STATE: HEATING"

            # Temp
            temp_tokens = temp_part.split()  # ["Temp:", "23.45"]
            temp_val = float(temp_tokens[1])
            self.temp_var.set(f"{temp_val:.2f} ")

            # SP
            sp_tokens = sp_part.split()  # ["SP:", "50.0"]
            sp_val = float(sp_tokens[1])
            self.sp_var.set(f"{sp_val:.1f} ")
            self.current_sp_value = sp_val

            # Only force slider + label to SP if user is *not* in the middle of adjusting it
            if not self.user_adjusting_slider and self.current_sp_value is not None:
                self.ignore_slider_change = True
                self.sp_slider_var.set(int(round(self.current_sp_value)))
                self.ignore_slider_change = False
                self.update_slider_label(self.current_sp_value)

            # State (string)
            state_tokens = state_part.split(":", 1)
            state_str = state_tokens[1].strip() if len(state_tokens) > 1 else state_part
            self.state_var.set(state_str)

            # --- NEW: use STATE to enable/disable Set button ---
            # Try to interpret leading token of state_str as an int (0 or 1)
            state_int = None
            try:
                state_int = int(state_str.split()[0])
            except ValueError:
                state_int = None

            if self.client is not None:
                if state_int == 1:
                    # Hotplate "busy"/locked → disable Set
                    self.set_button.config(state="disabled")
                elif state_int == 0:
                    # Hotplate ready → enable Set
                    self.set_button.config(state="normal")
            # -----------------------------------------------

            # Update histories for plotting
            self.sample_count += 1
            self.sample_indices.append(self.sample_count)
            self.temp_history.append(temp_val)
            self.sp_history.append(sp_val)

            # Keep only the last max_points
            if len(self.sample_indices) > self.max_points:
                self.sample_indices.pop(0)
                self.temp_history.pop(0)
                self.sp_history.pop(0)

            self._update_plot()

        except Exception:
            # If parsing fails, we just leave the labels/plot as-is.
            pass
    # ==============================
    # Shutdown
    # ==============================
    def on_close(self):
        if self.client is not None:
            self.client.close()
        self.master.destroy()


# ==============================
# Entry point
# ==============================
if __name__ == "__main__":
    root = tk.Tk()
    app = CentrifugeGUI(root)
    root.mainloop()