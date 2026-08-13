#include "crypto_ops.h"
#include "storage.h"
#include "tamper.h"

#include "psa/crypto.h"

volatile HsmState currentState = STATE_INIT;

extern "C" void app_main()
{
    currentState = STATE_INIT;

    psa_status_t status = psa_crypto_init();

    if (status != PSA_SUCCESS) {
        currentState = STATE_ERROR;
        return;
    }

    // ------------------------------------------------
    // Initialize cryptographic subsystem
    // ------------------------------------------------

    if (psa_crypto_init() != PSA_SUCCESS) {
        currentState = STATE_ERROR;
        return;
    }

    // ------------------------------------------------
    // Initialize NVS
    // ------------------------------------------------

    // init NVS here

     if (!keystore_init()) {
        currentState = STATE_ERROR;
        return;
    }


    // ------------------------------------------------
    // Initialize GPIO
    // ------------------------------------------------

    // gpio init here

    
    // ------------------------------------------------
    // Tamper initialization
    // ------------------------------------------------
    // key initialization
    // etc.
    if (!initTamper()) {
        currentState = STATE_ERROR;
        return;
    }
    
    // ------------------------------------------------
    // Self tests
    // ------------------------------------------------

    currentState = STATE_SELFTEST;
    runSelfTests();

    if (currentState == STATE_ERROR) {
        return;
    }


    // ------------------------------------------------
    // Main loop
    // ------------------------------------------------

    while (true) {
        // handle commands
        // tamper checking
        // state machine
    }
}
