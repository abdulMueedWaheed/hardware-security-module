import streamlit as st
from cli import parse_log_line
from crypto_utils import parse_pubkey, verify_signature
from hsm_client import HSMClient

st.set_page_config(page_title="ESP32 HSM Console", layout="centered")
st.title("🔐 ESP32-S3 HSM Console")

if "hsm" not in st.session_state:
    with st.spinner("Connecting to HSM..."):
        try:
            st.session_state.hsm = HSMClient()
            st.session_state.hsm.sync_time()
        except ConnectionError as e:
            st.error(str(e))
            st.stop()

hsm = st.session_state.hsm

status = hsm.status()
state = status.get("STATE", "UNKNOWN")
key_present = status.get("KEY_PRESENT", "NO")

col1, col2 = st.columns(2)
col1.metric("Device State", state)
col2.metric("Key Present", key_present)

st.divider()

if st.button("Generate Key"):
    with st.spinner("Waiting for physical button press on device..."):
        result = hsm.send_and_wait_for_auth("GENKEY")
    st.success(result) if result.startswith("OK") else st.error(result)

if key_present == "YES":
    pubkey_hex = hsm.send("GETPUBKEY")[0]
    st.code(pubkey_hex, language="text")

st.divider()

text_to_sign = st.text_input("Text to sign")
if st.button("Sign") and text_to_sign:
    hex_data = text_to_sign.encode("utf-8").hex()
    with st.spinner("Waiting for physical button press on device..."):
        sig = hsm.send_and_wait_for_auth(f"SIGN:{hex_data}")
    if sig.startswith("ERR") or sig == "":
        st.error(sig or "No response")
    else:
        pubkey_hex = hsm.send("GETPUBKEY")[0]
        vk = parse_pubkey(pubkey_hex)
        if vk and verify_signature(vk, sig, hex_data):
            st.success("Signature valid ✅")
            st.code(sig)
        else:
            st.error("Signature verification FAILED")

st.divider()

if st.button("View Audit Log"):
    log_lines = hsm.send("GETLOG")
    entries = [
        parse_log_line(l) for l in log_lines
        if l not in ("LOG_BEGIN", "LOG_END", "LOG_EMPTY")
    ]
    if entries:
        st.table(entries)
    else:
        st.info("No log entries yet.")

if st.button("Zeroize Keys", type="primary"):
    result = hsm.send_and_wait_for_auth("ZEROIZE")
    st.warning(result)