import re
import sys
from datetime import datetime

from crypto_utils import parse_pubkey, verify_signature
from hsm_client import HSMClient, clean_line

LOG_LINE_RE = re.compile(r"^#(\d+)\s+\[(\d+|unsynced)\]\s+(.+)$")


def parse_log_line(line: str) -> dict:
    cleaned = clean_line(line)
    m = LOG_LINE_RE.match(cleaned)
    if not m:
        return {"seq": None, "time": None, "event": cleaned}
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
    print("Commands: PING | STATUS | LDRVAL | GENKEY | GETPUBKEY | SIGN:<text> | ZEROIZE | GETTIME | GETLOG | EXIT\n")

    running = True
    while running:
        try:
            msg = input("Input to Chip: ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\nExiting...")
            break

        if msg == "":
            continue

        if msg.upper() == "EXIT":
            running = False

        elif msg.upper().startswith("SIGN:"):
            plaintext = msg[5:]
            hex_data = plaintext.encode("utf-8").hex()
            response = hsm.send_and_wait_for_auth(
                f"SIGN:{hex_data}",
                on_status=lambda l: print(">>>", l)
            )

            pub_lines = hsm.send("GETPUBKEY")
            pubkey_hex = pub_lines[0] if pub_lines else ""
            vk = parse_pubkey(pubkey_hex)

            if vk and verify_signature(vk, response, hex_data):
                print("SUCCESS (Signature Validated) —", response)
            else:
                print("FAILED (Signature Verification Failed) —", response)

        elif msg.upper() in ("GENKEY", "ZEROIZE"):
            print("Waiting for physical button press (Ctrl+C to abort)...")
            try:
                res = hsm.send_and_wait_for_auth(msg.upper(), on_status=lambda l: print(">>>", l))
                print(res)
            except KeyboardInterrupt:
                print("\nAborted in CLI — note: firmware may still complete operation.")

        else:
            for line in hsm.send(msg):
                parsed = parse_log_line(line)
                if parsed["seq"] is not None:
                    print(f"  #{parsed['seq']} [{parsed['time']}] {parsed['event']}")
                else:
                    print(parsed["event"])

    hsm.close()
    print("Ok bye!")


if __name__ == "__main__":
    main()