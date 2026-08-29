# Fallback flashing over the serial port

Use the normal route first:

```bash
STM32_Programmer_CLI -c port=SWD -w build/Release/KWS_TRON.hex -v -rst
```

These scripts exist for a host whose ST-LINK USB link cannot sustain the bulk
transfers SWD flashing needs, which was the case on one development machine
here. SWD is used only for a few seconds to jump the core into the on chip ROM
bootloader, then the image is written and verified over the virtual COM port
with the standard UART bootloader protocol.

```bash
openocd -f interface/stlink.cfg -c "transport select swd" \
        -f .vscode/stm32h533.cfg -f tools/flashing/jump_bootloader.tcl
KWS_PORT=/dev/tty.usbmodemXXXX python tools/flashing/uart_flash.py \
        build/Release/KWS_TRON.bin --erase
```

`uart_flash.py` contains no mass erase and no option byte command of any kind,
so it can only write and verify user flash. Every byte is read back and compared
before the firmware is started.
