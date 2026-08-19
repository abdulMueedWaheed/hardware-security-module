import streamlit as st
from cli import parse_log_line
from crypto_utils import parse_pubkey, verify_signature
from hsm_client import HSMClient, clean_line

st.set_page_config(page_title="ESP32 HSM Console", layout="centered")
st.title("🔐 ESP32-S3 HSM Console")

# Initialize and cache HSM connection in session state
if "hsm" not in st.session_state or not st.session_state.hsm.ser.is_open:
    with st.spinner("Connecting to HSM..."):
        try:
            st.session_state.hsm = HSMClient()
            sync_res = st.session_state.hsm.sync_time()
            st.toast(f"Time synced: {sync_res}", icon="⏰")
        except Exception as e:
            st.error(f"Serial Connection Failed: {e}")
            st.info("Ensure cli.py or minicom is closed in other terminal windows.")
            st.stop()

hsm = st.session_state.hsm

# Device status indicators
status = hsm.status()
state = status.get("STATE", "UNKNOWN")
key_present = status.get("KEY_PRESENT", "NO")

col1, col2 = st.columns(2)
col1.metric("Device State", state)
col2.metric("Key Present", key_present)

st.divider()

# Key Generation
st.subheader("🔑 Key Management")
if st.button("Generate Key Pair"):
    with st.spinner("Awaiting physical button press on HSM..."):
        result = hsm.send_and_wait_for_auth("GENKEY")
    if "OK" in result:
        st.success("Key generated successfully!")
        st.rerun()
    else:
        st.error(result)

# Display Public Key if available
if key_present == "YES":
    pub_lines = hsm.send("GETPUBKEY")
    if pub_lines:
        raw_pub = pub_lines[0]
        clean_pub = raw_pub.split(":", 1)[1] if ":" in raw_pub else raw_pub
        st.caption("Active ECC NIST P-256 Public Key (Uncompressed Hex):")
        st.code(clean_pub, language="text")

st.divider()

# Cryptographic Operations
st.subheader("✍️ Digital Signature")
text_to_sign = st.text_input("Plaintext Message to Sign")
if st.button("Sign Message") and text_to_sign:
    hex_data = text_to_sign.encode("utf-8").hex()
    
    with st.spinner("Awaiting physical button press on HSM..."):
        sig_response = hsm.send_and_wait_for_auth(f"SIGN:{hex_data}")
    
    if sig_response.startswith("ERR") or not sig_response:
        st.error(sig_response or "No response from HSM")
    else:
        # Extract signature hex
        clean_sig = sig_response.split(":", 1)[1] if ":" in sig_response else sig_response
        
        # Verify signature against device public key
        pub_lines = hsm.send("GETPUBKEY")
        pubkey_hex = pub_lines[0] if pub_lines else ""
        vk = parse_pubkey(pubkey_hex)
        
        if vk and verify_signature(vk, clean_sig, hex_data):
            st.success("Signature Validated Successfully ✅")
            st.code(clean_sig, language="text")
        else:
            st.error("Signature Verification Failed ❌")
            st.code(clean_sig, language="text")

st.divider()

# Audit Log Table
st.subheader("📜 Hardware Audit Log")
if st.button("Fetch Audit Log"):
    log_lines = hsm.send("GETLOG")
    table_rows = []
    
    for line in log_lines:
        cleaned = clean_line(line)
        if cleaned in ("LOG_BEGIN", "LOG_END", "LOG_EMPTY"):
            continue
        
        parsed = parse_log_line(cleaned)
        if parsed["seq"] is not None:
            table_rows.append({
                "Seq #": parsed["seq"],
                "Timestamp": parsed["time"],
                "Event": parsed["event"]
            })

    if table_rows:
        st.table(table_rows)
    else:
        st.info("No audit log entries recorded yet.")

st.divider()

# Emergency Zeroization
st.subheader("🚨 Emergency Operations")
if st.button("Zeroize Keys (Purge)", type="primary"):
    with st.spinner("Awaiting physical button press to confirm zeroization..."):
        result = hsm.send_and_wait_for_auth("ZEROIZE")
    if "ZEROIZED" in result:
        st.warning("All keys zeroized and securely erased!")
        st.rerun()
    else:
        st.error(result)