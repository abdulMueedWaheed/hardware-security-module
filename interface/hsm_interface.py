"""
HSM Host Interface (Python)
----------------------------
Talks to the ESP32-S3 HSM firmware over serial. Acts purely as a client:
it never touches private key material, it only requests services
(GENKEY, SIGN, GETPUBKEY, ZEROIZE, STATUS) and independently verifies
signatures returned by the device.

Usage:
    python hsm_interface.py [port]

    If no port is given, defaults to /dev/ttyACM0 (Linux). On Windows use
    something like COM3, on macOS something like /dev/tty.usbmodemXXXX.
"""

import hashlib
import sys
import time
import serial
from ecdsa import NIST256p, VerifyingKey
from ecdsa.util import sigdecode_der

DEFAULT_PORT = "/dev/ttyACM0"
BAUD = 115200


def connect(port: str) -> serial.Serial:
    ser = serial.Serial(port, baudrate=BAUD, timeout=3)
    time.sleep(2)  # allow the ESP32 to reset after the serial port opens
    return ser


def handleInput(ser: serial.Serial, cmd: str) -> str:
    ser.write((cmd + "\n").encode("utf-8"))
    return ser.readline().decode("ascii", errors="replace").strip()


def send_and_wait_for_auth(ser: serial.Serial, cmd: str) -> str:
    """
    Sends a command that may require physical button authorization on the
    device (GENKEY / SIGN / ZEROIZE). Surfaces intermediate status lines
    to the user and returns the final response line.
    """
    ser.write((cmd + "\n").encode("utf-8"))
    while True:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if line == "":
            print("No response from device (timeout). It may be busy, "
                  "disconnected, or the auth window expired.")
            return ""
        if line == "AWAITING_AUTH_PRESS_BUTTON":
            print(">>> Press the physical button on the HSM now to authorize this operation...")
            continue
        if line == "TAMPER_DETECTED_KEYS_ZEROIZED":
            print("!!! TAMPER DETECTED ON DEVICE - keys have been zeroized. !!!")
            return line
        return line


def get_pubkey(ser: serial.Serial):
    pubkey_hex = handleInput(ser, "GETPUBKEY")
    if pubkey_hex.startswith("ERR_") or pubkey_hex == "":
        print("Could not retrieve public key:", pubkey_hex or "(no response)")
        return None
    try:
        pubkey_bytes = bytes.fromhex(pubkey_hex)
        return VerifyingKey.from_string(pubkey_bytes, curve=NIST256p)
    except (ValueError, Exception) as e:
        print("Received malformed public key:", pubkey_hex, "-", e)
        return None


def validate(ser: serial.Serial, sig_hex: str, original_hex_data: str) -> bool:
    if sig_hex == "" or sig_hex.startswith("ERR_") or sig_hex == "TAMPER_DETECTED_KEYS_ZEROIZED":
        print("Signing did not succeed on device:", sig_hex or "(no response)")
        return False
    try:
        signature = bytes.fromhex(sig_hex)
    except ValueError:
        print("Bad signature format received:", sig_hex)
        return False

    vk = get_pubkey(ser)
    if vk is None:
        return False

    data = bytes.fromhex(original_hex_data)
    digest = hashlib.sha256(data).digest()
    try:
        # mbedtls_ecdsa_write_signature outputs DER-encoded signatures
        return vk.verify_digest(signature, digest, sigdecode=sigdecode_der)
    except Exception as e:
        print("Verification failed:", e)
        return False


def wait_for_selftest(ser: serial.Serial):
    print("Waiting for device self-test result...")
    start = time.time()
    while time.time() - start < 5:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line:
            continue
        print("[DEVICE]", line)
        if line == "SELFTEST_PASS":
            print("Device self-test passed. HSM is ready.\n")
            return True
        if line == "SELFTEST_FAIL":
            print("Device self-test FAILED. HSM will refuse commands until reset/fixed.\n")
            return False
    print("No self-test result seen (device may already be past boot). Continuing.\n")
    return True


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    print(f"Connecting to HSM on {port} ...")
    try:
        ser = connect(port)
    except serial.SerialException as e:
        print(f"Could not open port {port}: {e}")
        print("Pass a different port as an argument, e.g.: python hsm_interface.py COM3")
        return

    wait_for_selftest(ser)

    print("Commands: PING | STATUS | LDRVAL | GENKEY | GETPUBKEY | SIGN:<text> | ZEROIZE | EXIT\n")

    running = True
    while running:
        msg = input("Input to Chip: ").strip()
        if msg == "":
            continue

        if msg == "EXIT":
            running = False

        elif msg.upper().startswith("SIGN:"):
            plaintext = msg[5:]
            hex_data = plaintext.encode("utf-8").hex()
            response = send_and_wait_for_auth(ser, f"SIGN:{hex_data}")
            if validate(ser, response, hex_data):
                print("SUCCESS")
                print("Signature:", response)
            else:
                print("FAILED")

        elif msg.upper() in ("GENKEY", "ZEROIZE"):
            response = send_and_wait_for_auth(ser, msg.upper())
            print(response)

        else:
            print(handleInput(ser, msg))

    ser.close()
    print("Ok bye!")


if __name__ == "__main__":
    main()
