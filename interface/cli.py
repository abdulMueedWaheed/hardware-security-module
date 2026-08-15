import re
import sys
from datetime import datetime

from crypto_utils import parse_pubkey, verify_signature
from hsm_client import HSMClient

LOG_LINE_RE = re.compile(r"^#(\d+) \[(\d+|unsynced)\] (.+)$")

def parse_log_line(line: str) -> dict:
    m = LOG_LINE_RE.match(line)
    if not m:
        return {"seq": None, "time": None, "event": line}
    seq, ts, event = m.groups()
    if ts == "unsynced":
        time_str = "— (unsynced)"
    else:
        time_str = datetime.fromtimestamp(int(ts)).strftime("%Y-%m-%d %H:%M:%S")
    return {"seq": int(seq), "time": time_str, "event": event}


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    print(f"Connecting to HSM on {port} ...")
    try:
        hsm = HSMClient(port)
    except ConnectionError as e:
        print(e)
        return

    status = hsm.status()
    print(f"[DEVICE STATE] {status.get('STATE', 'UNKNOWN')}")
    if status.get("STATE") == "ERROR":
        print("Self-test failed on device.\n")

    print(hsm.sync_time())
    print("Commands: PING | STATUS | LDRVAL | GENKEY | GETPUBKEY | SIGN:<text> | ZEROIZE | GETLOG | EXIT\n")

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
            response = hsm.send_and_wait_for_auth(
                f"SIGN:{hex_data}",
                on_status=lambda l: print(">>>", l)
            )
        
            pubkey_hex = hsm.send("GETPUBKEY")[0]
            vk = parse_pubkey(pubkey_hex)
        
            if vk and verify_signature(vk, response, hex_data):
                print("SUCCESS —", response)
            else:
                print("FAILED —", response)
        
        elif msg.upper() in ("GENKEY", "ZEROIZE"):
            print("Waiting for button press (Ctrl+C to abort)...")
            try:
                print(hsm.send_and_wait_for_auth(msg.upper(), on_status=lambda l: print(">>>", l)))
            except KeyboardInterrupt:
                print("\nAborted — note: firmware may still complete the operation independently.")
        
        else:
            for line in hsm.send(msg):
                print(parse_log_line(line))

    hsm.close()
    print("Ok bye!")

if __name__ == "__main__":
    main()