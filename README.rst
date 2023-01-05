
NEC IR Decoder
######

Overview
********
This application will capture NEC IR Encoded frames on MISO_PIN using the nRF52840's SPIM0 peripheral.

Requirements
************
nRF52840 series device with at least one available SPIM peripheral. NCS v2.2.0 SDK or newer. 

Building and Running
********************
Using the nRF Connect for VS Code extension:

Click the '+' button labeled "nRF Connect: Add Folder As Application" in the APPLICATIONS pane.
You will need to hover over the pane beyfore the button row will appear.

Add a build configuration for your nRF52840 device, then build and flash the device.

Additional info
***************
A companion NEC IR Encoder application can be found `here<https://github.com/haakonsh/NEC_IR_Encoder.git>`_.

nRF52840 has 16-bit buffer sizes for the SPIM peripheral vs nRF52832's 8-bit. Even at the lowest SPI frequencies an NEC IR encoded packet will need ~900 bytes. 
The SPIM's `RXD.LIST register<https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/spim.html?cp=4_2_0_30_5_11#register.RXD.LIST>'_. can be used to increase the buffer size to fully capture the whole packet, but it is not implemented. See 'EasyDMA list<https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.nrf52832.ps.v1.1/spim.html?cp=4_2_0_30_1_0#topic>`_ chapter for more information.

`NEC IR protocol appnote from Altium <https://techdocs.altium.com/display/FPGA/NEC%2bInfrared%2bTransmission%2bProtocol>`_

I recommend the NEC Decoder extension for Saleae Logic 2 if you need to inspect and decode NEC IR packets.