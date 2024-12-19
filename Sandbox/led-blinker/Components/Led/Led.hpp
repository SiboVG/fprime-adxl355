// ======================================================================
// \title  Led.hpp
// \author sibo
// \brief  hpp file for Led component implementation class
// ======================================================================

#ifndef Components_Led_HPP
#define Components_Led_HPP

#include "Components/Led/LedComponentAc.hpp"

namespace Components {

class Led : public LedComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct Led object
    Led(const char* const compName  //!< The component name
    );

    //! Destroy Led object
    ~Led();

    PRIVATE :
        // ----------------------------------------------------------------------
        // Member variables
        // ----------------------------------------------------------------------

        Fw::On m_ledState = Fw::On::OFF;  //! Keeps track if LED is on or off
    U64 m_transitions = 0;                //! Number of on/off transitions that have occurred from FSW boot up
    U32 m_toggleCounter = 0;              //! Counter to keep track of the number of toggles
    bool m_blinking = false;              //! Flag: if true then LED blinking will occur else no blinking will happen

    PRIVATE :
        // ----------------------------------------------------------------------
        // Handler implementations for user-defined typed input ports
        // ----------------------------------------------------------------------

        //! Handler implementation for run
        //!
        //! Port to receive the blinking calls from the rate group
        void
        run_handler(FwIndexType portNum,  //!< The port number
                    U32 context           //!< The call order
                    ) override;

    PRIVATE :
        // ----------------------------------------------------------------------
        // Handler implementations for commands
        // ----------------------------------------------------------------------

        //! Handler implementation for command BLINKING_ON_OFF
        //!
        //! Command to start/stop blinking the LED
        void
        BLINKING_ON_OFF_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                   U32 cmdSeq,           //!< The command sequence number
                                   Fw::On onOff          //!< "Blinking state (ON/OFF)"
                                   ) override;

    PRIVATE :
        // ----------------------------------------------------------------------
        // Handler implementations for telemetry
        // ----------------------------------------------------------------------

        //! Emit parameter updated EVR
        //!
        void
        parameterUpdated(FwPrmIdType id  //!< The parameter ID
                         ) override;
};

}  // namespace Components

#endif
