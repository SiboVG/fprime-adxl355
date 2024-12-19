// ======================================================================
// \title  Led.cpp
// \author sibo
// \brief  cpp file for Led component implementation class
// ======================================================================

#include "Components/Led/Led.hpp"
#include "FpConfig.hpp"

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

Led ::Led(const char* const compName) : LedComponentBase(compName) {
}

Led ::~Led() {}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

void Led ::run_handler(FwIndexType portNum, U32 context) {
    // Read back the interval parameter
    Fw::ParamValid isValid = Fw::ParamValid::INVALID;
    U32 interval = this->paramGet_BLINK_INTERVAL(isValid);
    FW_ASSERT(isValid == Fw::ParamValid::VALID, static_cast<FwAssertArgType>(isValid));

    // If the LED is blinking, toggle the state
    if (this->m_blinking && (interval != 0)) {
        if (this->m_toggleCounter == 0) {
            // Toggle the LED state
            this->m_ledState = (this->m_ledState == Fw::On::ON) ? Fw::On::OFF : Fw::On::ON;
            this->m_transitions++;
            this->tlmWrite_LedTransitions(this->m_transitions);

            // Port may not be connected, so check before sending output
            if (this->isConnected_gpioSet_OutputPort(0)) {
                Fw::Logic output = (this->m_ledState == Fw::On::ON) ? Fw::Logic::HIGH : Fw::Logic::LOW;
                this->gpioSet_out(0, output);
            }

            // Fire an event change
            this->log_ACTIVITY_LO_LedState(this->m_ledState);
        }

        this->m_toggleCounter = (this->m_toggleCounter + 1) % interval;
    }
    // We are not blinking, so just turn the LED off
    else {
        if (this->m_ledState == Fw::On::ON) {
            // Port may not be connected, so check before sending output
            if (this->isConnected_gpioSet_OutputPort(0)) {
                this->gpioSet_out(0, Fw::Logic::LOW);
            }

            this->m_ledState = Fw::On::OFF;
            this->log_ACTIVITY_LO_LedState(this->m_ledState);
        }
    }
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void Led ::BLINKING_ON_OFF_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Fw::On onOff) {
    // Reset the toggle counter
    this->m_transitions = 0;
    this->m_toggleCounter++;

    // Set the blinking state and report an event
    this->m_blinking = onOff == Fw::On::ON;
    this->log_ACTIVITY_HI_BlinkingStateSet(onOff);

    // Set the telemetry value
    this->tlmWrite_BlinkingState(onOff);

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Handler implementations for telemetry
// ----------------------------------------------------------------------

void Led ::parameterUpdated(FwPrmIdType id) {
    Fw::ParamValid isValid = Fw::ParamValid::INVALID;
    switch (id) {
        case PARAMID_BLINK_INTERVAL: {
            // Read back the parameter value
            const U32 interval = this->paramGet_BLINK_INTERVAL(isValid);
            // NOTE: isValid is always VALID in parameterUpdated as it was just properly set
            FW_ASSERT(isValid == Fw::ParamValid::VALID, static_cast<FwAssertArgType>(isValid));

            // Emit the blink interval set event
            this->log_ACTIVITY_HI_BlinkingIntervalSet(interval);
            break;
        }
        default:
            FW_ASSERT(0, static_cast<FwAssertArgType>(id));
            break;
    }
}

}  // namespace Components
