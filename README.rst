
NEC IR Decoder
######

Overview
********
This application will capture NEC IR Encoded frames on MISO_PIN using the nRF52840's SPIM0 peripheral.

The following logical analyzer capture (using a Saleae Logic PRO 8 with the NEC Decoder extension) shows how the target nRF52840's SPIM 
starts a transfer after detecting a start frame condition (logic high pulse of at least 7.5ms on MISO_PIN), synchronized
to the rising flank of the first information symbol. 

The target nRF52840 also outputs an SCK signal that is used as a trigger source for the logical analyzer capture. 
The SCK signal can be disabled by setting SCK_PIN in main.c to NRFX_SPIM_PIN_NOT_USED. 
.. image:: doc/NEC_IR_packet.png

You can open `this <doc/NEC_IR_saleae_capture.sal>`_ capture file with Saleae's Logic 2.x SW if you want to study it further.

Here is the start the terminal output of the application when receiving packets from the companion NEC IR Encoder application,
 found `here <https://github.com/haakonsh/NEC_IR_Encoder.git>`_:

 .. literalinclude:: doc/terminal_output.txt
    :language: txt

Requirements
************
nRF52840 series device with at least one available SPIM peripheral, one RTC or TIMER peripheral, 2 GPIOTE channels,
, 4 PPI channels, and one PPI cannel group. NCS v2.2.0 SDK or newer. 

Building and Running
********************
Using the nRF Connect for VS Code extension:

Click the '+' button labeled "nRF Connect: Add Folder As Application" in the APPLICATIONS pane.
You will need to hover over the pane beyfore the button row will appear.

Add a build configuration for your nRF52840 device, then build and flash the device. 
Connect your nRF52840 device to an appropriate terminal to view the received data. 

Additional info
***************
The nRF52840 has 16-bit buffer sizes for the SPIM peripheral where the nRF52832 only has 8-bit. Even at the lowest SPI frequencies an NEC IR encoded packet will need ~900 bytes. 
The SPIM's `RXD.LIST <https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/spim.html?cp=4_2_0_30_5_11#register.RXD.LIST>`_ register can be used to increase the buffer size to fully capture the whole packet, but it is not implemented.
See `EasyDMA list <https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/spim.html?cp=4_2_0_30_1_0#topic>`_ chapter for more information.


`NEC IR protocol appnote from Altium <https://techdocs.altium.com/display/FPGA/NEC%2bInfrared%2bTransmission%2bProtocol>`_.


I recommend the NEC Decoder extension for Saleae Logic 2 if you need to inspect and decode NEC IR packets.