#include "state.h"
#include <string>

volatile HsmState currentState = STATE_INIT;

const char * stateToString(HsmState state) {

    switch (state) {
        case STATE_IDLE:
            return "IDLE";

        case STATE_AWAITING_AUTH:
            return "AWAITING_AUTH";

        case STATE_PROCESSING:
            return "PROCESSING";

        case STATE_TAMPER_LOCKED:
            return "TAMPER_LOCKED";

        case STATE_ERROR:
            return "ERROR";

        case STATE_INIT:
            return "INIT";

        case STATE_SELFTEST:
            return "SELFTEST";

        default:
            return "UNKNOWN";
    }
}