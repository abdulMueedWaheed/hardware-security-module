import time
import readline

import serial

DEFAULT_PORT = "/dev/ttyACM0"
BAUD = 115200


class HSMClient:
    def __init__(self, port: str = DEFAULT_PORT, wait_seconds: int = 5):
        self.ser = self._connect(port, wait_seconds)

    def _connect(self, port, wait_seconds):
        start = time.time()
        last_error = None
        while time.time() - start < wait_seconds:
            try:
                ser = serial.Serial(port, baudrate=BAUD, timeout=3)
                time.sleep(2)
                ser.reset_input_buffer()
                return ser
            except serial.SerialException as e:
                last_error = e
                time.sleep(0.5)
        raise ConnectionError(f"Could not connect to {port} after {wait_seconds}s: {last_error}")

    def read_multiline(self, idle_timeout=0.8) -> list[str]:
        lines = []
        self.ser.timeout = idle_timeout
        while True:
            line = self.ser.readline().decode("ascii", errors="replace").strip()
            if line == "":
                break
            lines.append(line)
        self.ser.timeout = 3
        return lines

    def send(self, cmd: str) -> list[str]:
        self.ser.write((cmd + "\n").encode("utf-8"))
        return self.read_multiline()

    def send_and_wait_for_auth(self, cmd: str, on_status=None) -> str:
        TERMINAL_TOKENS = (
            "OK_KEY_GENERATED",
            "OK_ZEROIZED",
            "ERR_AUTH_TIMEOUT",
            "TAMPER_DETECTED_KEYS_ZEROIZED",
        )

        self.ser.write((cmd + "\n").encode("utf-8"))
        self.ser.timeout = None
        try:
            while True:
                raw = self.ser.readline()
                line = raw.decode("ascii", errors="replace").strip()

                if line == "":
                    continue  # blank line on the wire, not a timeout (timeout=None can't time out)

                if "AWAITING_AUTH_PRESS_BUTTON" in line:
                    if on_status:
                        on_status(line)
                    continue

                if any(tok in line for tok in TERMINAL_TOKENS):
                    if on_status and "TAMPER_DETECTED" in line:
                        on_status(line)
                    return line

                # Unrecognized line (e.g. an unrelated ESP_LOGI/W/E line)
                # Don't treat it as the answer — surface it and keep waiting.
                if on_status:
                    on_status(line)
                continue
        finally:
            self.ser.timeout = 3

    def status(self) -> dict:
        lines = self.send("STATUS")
        result = {}
        for l in lines:
            if ":" in l:
                k, v = l.split(":", 1)
                result[k] = v
        return result

    def sync_time(self) -> str:
        lines = self.send(f"SETTIME:{int(time.time())}")
        return lines[0] if lines else ""

    def encrypt_flash(self, enable: bool = True) -> str:
        cmd = "ENCRYPT:ON" if enable else "ENCRYPT:OFF"
        lines = self.send(cmd)
        return lines[0] if lines else ""

    def close(self):
        self.ser.close()