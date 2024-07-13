# Experimental Wireless Fork of Zephyr™ Mechanical Keyboard (ZMK) Firmware

This Fork is testing using some of Nordic's proprietery low latency protocols to improve latency between a wireless keyboard and the host device. it requires a nordic RF chip to use and specifically any nRF5 chip for ESB and nRF52 chip for LLPM. All of my testing was done on devices with nRF52840 chips.

### Hardware:
- 2 x [MDBT50Q-RX dongles](https://www.digikey.com/en/products/detail/adafruit-industries-llc/5199/15189159)
    - One will be the receiver/central and the other the transmitter/peripheral
    - You cannot communicate directly to the host device with these protocols unless your device is running a Nordic nRF5 chip and you know how to enable the necessary features. That is why we are using the dongle approach that is connected to the host device via USB

### Protocols:
- [Enhanced Shock Burst (ESB)](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/protocols/esb/index.html)
- [Low Latency Packet Mode (LLPM)](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/samples/bluetooth/llpm/README.html)

### Configuration:
*all configurations can be updated in the [mdbt50q_rx_defconfig](app/boards/arm/mdbt50q_rx/mdbt50q_rx_defconfig) file*
- ESB
    - Receiver:
        - `CONFIG_ZMK_ESB=y`
        - `CONFIG_ZMK_ESB_RECEIVER=y`
    - Transmitter:
        - `CONFIG_ZMK_ESB=y`
        - `CONFIG_ZMK_ESB_RECEIVER=n`
    - Other Options:
        - `CONFIG_ESB_FAST_RAMP_UP` *For nRF52 and nRF53 Series devices, enable to reduce the delay when switching from tx/rx and vice-versa. see [Fast ramp-up](https://docs.nordicsemi.com/bundle/ncs-2.6.1/page/nrf/protocols/esb/index.html#fast_ramp-up)*
        - `CONFIG_ZMK_ESB_DYNAMIC_PAYLOAD` *Enable to use the legacy ESB dynamic payload size instead of the default static payload size*
- LLPM
    - Central:
        - `CONFIG_ZMK_BLE=y`
        - `CONFIG_ZMK_SPLIT_LLPM=y`
        - `CONFIG_ZMK_SPLIT_ROLE_CENTRAL=y`
    - Peripheral:
        - `CONFIG_ZMK_BLE=y`
        - `CONFIG_ZMK_SPLIT_LLPM=y`
        - `CONFIG_ZMK_SPLIT_ROLE_CENTRAL=n`

### Building:
- Setup your ZMK development enviornment by following the [wiki guide](https://zmk.dev/docs/development/setup#environment-setup): 
- `west build -p -b mdbt50q_rx`
- Don't forget to update CENTRAL/RECEIVER config before flashing your secondary device
- If you are using different hardware, I'd recommend looking at my [Kconfig.defconfig](app/boards/arm/mdbt50q_rx/Kconfig.defconfig) and [mdbt50q_rx_defconfig]app/boards/arm/mdbt50q_rx/mdbt50q_rx_defconfig) files for what you might need to copy over to your own board config to get this working

### Results:
- Latency - w/ eager debouncing (0ms down 5ms up) - *More detailed Latency results can be found in [joelspadin/keyboard-latency-tester](https://github.com/joelspadin/keyboard-latency-tester)*
    - ESB:
        - Static payload size: approx 1.1ms
        - Dynamic payload size: approx 1.1ms
    - BLE LLPM: approx 3.1ms 
- Power Consumption:
    - WIP

### Issues:
- If you flash the peripheral BLE LLPM device after it was already connected the Central BLE LLPM device, the Central device may not connect to it automatically. To fix: reflash the Central device again/last.

<br>

---
<br>

[![Discord](https://img.shields.io/discord/719497620560543766)](https://zmk.dev/community/discord/invite)
[![Build](https://github.com/zmkfirmware/zmk/workflows/Build/badge.svg)](https://github.com/zmkfirmware/zmk/actions)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-v2.0%20adopted-ff69b4.svg)](CODE_OF_CONDUCT.md)

[ZMK Firmware](https://zmk.dev/) is an open source ([MIT](LICENSE)) keyboard firmware built on the [Zephyr™ Project](https://www.zephyrproject.org/) Real Time Operating System (RTOS). ZMK's goal is to provide a modern, wireless, and powerful firmware free of licensing issues.

Check out the website to learn more: https://zmk.dev/.

You can also come join our [ZMK Discord Server](https://zmk.dev/community/discord/invite).

To review features, check out the [feature overview](https://zmk.dev/docs/). ZMK is under active development, and new features are listed with the [enhancement label](https://github.com/zmkfirmware/zmk/issues?q=is%3Aissue+is%3Aopen+label%3Aenhancement) in GitHub. Please feel free to add 👍 to the issue description of any requests to upvote the feature.
