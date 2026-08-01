import hashlib
import time

import serial
from ecdsa import NIST256p, VerifyingKey
from ecdsa.util import sigdecode_der

ser = serial.Serial("/dev/ttyACM0", baudrate=115200, timeout=3)
time.sleep(2)
print("Welcome!")


def handleInput(cmd: str) -> str:
    ser.write((cmd + "\n").encode("utf-8"))
    return ser.readline().decode("ascii", errors="replace").strip()


def get_pubkey() -> VerifyingKey:
    pubkey_hex = handleInput("GETPUBKEY")
    pubkey_bytes = bytes.fromhex(pubkey_hex)
    
    return VerifyingKey.from_string(pubkey_bytes, curve=NIST256p)


def validate(sig_hex: str, original_hex_data: str) -> bool:
    try:
        signature = bytes.fromhex(sig_hex)
    except ValueError:
        print("Bad signature format received:", sig_hex)
        return False

    vk = get_pubkey()
    data = bytes.fromhex(original_hex_data)
    digest = hashlib.sha256(data).digest()

    try:
        # mbedtls_ecdsa_write_signature outputs DER-encoded signatures
        return vk.verify_digest(signature, digest, sigdecode=sigdecode_der)
    except Exception as e:
        print("Verification failed:", e)
        return False


running: bool = True

while running:
    msg = input("Input to Chip: ")
    if msg == "EXIT":
        running = False
    elif msg[:5] == "SIGN:":
        plaintext = msg[5:]
        hex_data = plaintext.encode("utf-8").hex()
        response = handleInput(f"SIGN:{hex_data}")
        if validate(response, hex_data):
            print("SUCCESS")
            print("Signature:", response)
        else:
            print("FAILED")
    else:
        print(handleInput(msg))

print("Ok bye!")