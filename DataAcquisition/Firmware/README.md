# Acoustic Impedance Tube DAQ - Firmware

This repository contains the STM32 firmware for the Acoustic Impedance Tube Data Acquisition (DAQ) system. It is designed to sample acoustic data from a microphone and transmit it in real-time to a connected PC dashboard.

## Hardware Overview
* **Microcontroller:** STM32F401 series
* **Sensors:** Analog Microphone (Currently configured for 1 channel, with plans to expand to 4)
* **Interface:** UART over USB

## How It Works

The firmware operates a continuous data acquisition loop:
1. **Sampling:** It triggers a 12-bit ADC conversion on `ADC_CHANNEL_0`.
2. **Framing:** The 12-bit result is split into two bytes (LSB and MSB).
3. **Transmission:** A 4-byte data packet is constructed and sent over USART1 at a high baud rate of **921600 bps**.

### Data Packet Protocol
Each transmitted frame consists of 4 bytes to ensure the receiving dashboard can reliably synchronize and reconstruct the data stream:

| Byte 0 | Byte 1 | Byte 2 | Byte 3 |
|--------|--------|--------|--------|
| LSB    | MSB    | `0xAA` | `0xBB` |

* **Byte 0-1:** 12-bit ADC microphone reading.
* **Byte 2-3:** Constant termination sequence (`0xAA`, `0xBB`) used by the dashboard for alignment.

## Future Development
The project is currently configured for a single-microphone test setup. Future iterations will expand the ADC configuration to use Scan Mode and DMA to support all 4 microphones required for the complete acoustic impedance tube array.


