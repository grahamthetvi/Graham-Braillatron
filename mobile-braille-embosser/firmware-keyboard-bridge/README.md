# Phone-to-USB-HID typing bridge

ESP32-S3 sketch: a phone writes Nordic UART over BLE, and the chip types those bytes as a USB HID keyboard.

This is not the Braillatron embosser, and it is not the motor-rail safety co-processor. The safety co-processor is [`firmware-arduino/`](../firmware-arduino/) (Arduino Micro). The embosser pipeline is [`firmware-embosser/`](../firmware-embosser/).

The repo CI job compiles `firmware-arduino` for `arduino:avr:micro`. This sketch is not part of that build. It needs an ESP32 Arduino core, which this tree does not install.
