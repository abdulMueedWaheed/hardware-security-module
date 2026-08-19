import hashlib
from ecdsa import NIST256p, VerifyingKey
from ecdsa.util import sigdecode_der, sigdecode_string


def parse_pubkey(pubkey_hex: str) -> VerifyingKey | None:
    if not pubkey_hex or pubkey_hex.startswith("ERR_"):
        return None
    
    # Strip optional "PUBKEY:" prefix
    if ":" in pubkey_hex:
        pubkey_hex = pubkey_hex.split(":", 1)[1]

    try:
        return VerifyingKey.from_string(bytes.fromhex(pubkey_hex.strip()), curve=NIST256p)
    except Exception:
        return None


def verify_signature(vk: VerifyingKey, sig_hex: str, original_hex_data: str) -> bool:
    try:
        # Strip optional "SIGNATURE:" prefix
        if ":" in sig_hex:
            sig_hex = sig_hex.split(":", 1)[1]

        sig_bytes = bytes.fromhex(sig_hex.strip())
        digest = hashlib.sha256(bytes.fromhex(original_hex_data)).digest()

        # ESP32 uses raw 64-byte (r || s) signature format
        if len(sig_bytes) == 64:
            return vk.verify_digest(sig_bytes, digest, sigdecode=sigdecode_string)
        
        return vk.verify_digest(sig_bytes, digest, sigdecode=sigdecode_der)
    except Exception:
        return False