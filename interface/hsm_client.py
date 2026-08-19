import re
import time
import serial

DEFAULT_PORT = "/dev/ttyACM0"
BAUD = 115200

# Strips ESP-IDF log prefixes like "I (12345) TAG: "
LOG_PREFIX_RE = re.compile(r"^(?:[IWEV]\s*\(\d+\)\s*[\w_]+:\s*)?")


def clean_line(line: str) -> str:
    return LOG_PREFIX_RE.sub("", line).strip()


class HSMClient:
    def __init__(self, port: str = DEFAULT_PORT, wait_seconds: int = 5):
        self.ser = self._connect(port, wait_seconds)

    def _connect(self, port, wait_seconds):
        start = time.time()
        last_error = None
        while time.time() - start < wait_seconds:
            try:
                ser = serial.Serial(port, baudrate=BAUD, timeout=3)
                time.sleep(1.5)
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
            raw = self.ser.readline().decode("ascii", errors="replace")
            if not raw:
                break
            cleaned = clean_line(raw)
            if cleaned:
                lines.append(cleaned)
        self.ser.timeout = 3
        return lines

    def send(self, cmd: str) -> list[str]:
        self.ser.write((cmd.strip() + "\n").encode("utf-8"))
        return self.read_multiline()

    def send_and_wait_for_auth(self, cmd: str, on_status=None) -> str:
        TERMINAL_PREFIXES = (
            "OK_KEY_GENERATED",
            "OK_ZEROIZED",
            "SIGNATURE:",
            "ERR_",
            "TAMPER_DETECTED",
        )

        self.ser.write((cmd.strip() + "\n").encode("utf-8"))
        self.ser.timeout = None  # Block until button press or response
        try:
            while True:
                raw = self.ser.readline().decode("ascii", errors="replace")
                if not raw:
                    continue

                cleaned = clean_line(raw)
                if not cleaned:
                    continue

                if "AWAITING_AUTH_PRESS_BUTTON" in cleaned:
                    if on_status:
                        on_status(cleaned)
                    continue

                # Check if this line is our final answer
                if any(cleaned.startswith(prefix) for prefix in TERMINAL_PREFIXES):
                    return cleaned

                if on_status:
                    on_status(cleaned)
        finally:
            self.ser.timeout = 3

    def status(self) -> dict:
        lines = self.send("STATUS")
        result = {}
        for l in lines:
            if ":" in l:
                k, v = l.split(":", 1)
                result[k.strip()] = v.strip()
        return result

    def sync_time(self) -> str:
        lines = self.send(f"SETTIME:{int(time.time())}")
        return lines[0] if lines else ""

    def close(self):
        self.ser.close()