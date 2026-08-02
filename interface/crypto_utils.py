import hashlib

from ecdsa import NIST256p, VerifyingKey
from ecdsa.util import sigdecode_der


def parse_pubkey(pubkey_hex: str) -> VerifyingKey | None:
    if not pubkey_hex or pubkey_hex.startswith("ERR_"):
        return None
    try:
        return VerifyingKey.from_string(bytes.fromhex(pubkey_hex), curve=NIST256p)
    except Exception:
        return None


def verify_signature(vk: VerifyingKey, sig_hex: str, original_hex_data: str) -> bool:
    try:
        signature = bytes.fromhex(sig_hex)
        digest = hashlib.sha256(bytes.fromhex(original_hex_data)).digest()
        return vk.verify_digest(signature, digest, sigdecode=sigdecode_der)
    except Exception:
        return False