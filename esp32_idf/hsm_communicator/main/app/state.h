#ifndef STATE_H
#define STATE_H

#include <string>

// ----------------------------------------------------
// State Machine
// ----------------------------------------------------

enum HsmState {
    STATE_INIT,
    STATE_SELFTEST,
    STATE_IDLE,
    STATE_AWAITING_AUTH,
    STATE_PROCESSING,
    STATE_TAMPER_LOCKED,
    STATE_ERROR
};

extern volatile HsmState currentState;

const char* stateToString(HsmState state);

#endif