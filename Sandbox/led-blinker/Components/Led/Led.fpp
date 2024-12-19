module Components {
    @ Component to blink an LED driven by a rate group
    active component Led {

        #################################################
        #                     PORTS                     #
        #################################################

        @ Port to receive the blinking calls from the rate group
        async input port run: Svc.Sched

        @ Port sending calls to the GPIO driver
        output port gpioSet: Drv.GpioWrite

        ####################################################
        #                     COMMANDS                     #
        ####################################################

        @ Command to start/stop blinking the LED
        async command BLINKING_ON_OFF(
            onOff: Fw.On @< "Blinking state (ON/OFF)"
        )

        ##################################################
        #                     EVENTS                     #
        ##################################################

        @ Event that reports a blinking state change
        event BlinkingStateSet(
            $state: Fw.On   @< "Blinking state (ON/OFF)"
        ) severity activity high format "Blinking state set to {}"

        @ Event that reports a change in the blinking interval
        event BlinkingIntervalSet(
            interval: U32   @< "Blinking interval in milliseconds"
        ) severity activity high format "Blinking interval set to {}"

        @ Event that reports the LED state
        event LedState(
            $state: Fw.On   @< "LED state (ON/OFF)"
        ) severity activity low format "Led state {}"

        #####################################################
        #                     TELEMETRY                     #
        #####################################################

        @ Telemetry channel to report the blinking state
        telemetry BlinkingState: Fw.On

        @ Telemetry channel to report the number of LED transitions
        telemetry LedTransitions: U64

        ######################################################
        #                     PARAMETERS                     #
        ######################################################

        @ Parameter to set the blinking interval in rate
        param BLINK_INTERVAL: U32 default 1

        ##############################################################################
        #### Uncomment the following examples to start customizing your component ####
        ##############################################################################

        # @ Example async command
        # async command COMMAND_NAME(param_name: U32)

        # @ Example telemetry counter
        # telemetry ExampleCounter: U64

        # @ Example event
        # event ExampleStateEvent(example_state: Fw.On) severity activity high id 0 format "State set to {}"

        # @ Example port: receiving calls from the rate group
        # sync input port run: Svc.Sched

        # @ Example parameter
        # param PARAMETER_NAME: U32

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut

        @ Port for sending textual representation of events
        text event port logTextOut

        @ Port for sending events to downlink
        event port logOut

        @ Port for sending telemetry channels to downlink
        telemetry port tlmOut

        @ Port to return the value of a parameter
        param get port prmGetOut

        @Port to set the value of a parameter
        param set port prmSetOut

    }
}