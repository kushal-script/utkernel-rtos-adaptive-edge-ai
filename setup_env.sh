#!/usr/bin/env zsh
# Activate the TRON KWS development environment.
# Run: source setup_env.sh

TRON_DIR="$(cd "$(dirname "$0")" && pwd)"

# Python virtual environment
source "$TRON_DIR/.venv/bin/activate"

# ARM toolchain (Homebrew formula)
export PATH="/opt/homebrew/bin:$PATH"
export ARM_TOOLCHAIN="$(brew --prefix arm-none-eabi-gcc 2>/dev/null)/bin"
[ -d "$ARM_TOOLCHAIN" ] && export PATH="$ARM_TOOLCHAIN:$PATH"

# ST tools — downloaded by VS Code STM32 extension Bundles Manager
ST_BUNDLES="$HOME/Library/Application Support/stm32cube/bundles"
export PATH="$ST_BUNDLES/programmer/2.22.0+st.1/bin:$PATH"
export PATH="$ST_BUNDLES/stlink-gdbserver/7.13.0+st.3/bin:$PATH"
export PATH="$ST_BUNDLES/stlink-server/2.1.2+st.2/bin:$PATH"
export PATH="$ST_BUNDLES/gnu-gdb-for-stm32/14.3.1+st.2/bin:$PATH"

# Convenience aliases
alias serial-port='ls /dev/tty.usb*'
alias flash='STM32_Programmer_CLI -c port=SWD -d $TRON_DIR/build/Debug/KWS_TRON.elf -rst'
alias connect='screen $(ls /dev/tty.usb* 2>/dev/null | head -1) 115200'
alias probe='STM32_Programmer_CLI -c port=SWD'

echo "TRON KWS env active — Python: $(python3 --version), arm-gcc: $(arm-none-eabi-gcc --version 2>/dev/null | head -1 || echo 'not found')"
