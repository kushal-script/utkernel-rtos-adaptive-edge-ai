#!/usr/bin/env zsh
# Activate the TRON KWS development environment.
# Run: source setup_env.sh

TRON_DIR="$(cd "$(dirname "$0")" && pwd)"

# Python virtual environment
source "$TRON_DIR/.venv/bin/activate"

# ARM toolchain (Homebrew gcc-arm-embedded)
export PATH="/opt/homebrew/bin:$PATH"
export ARM_TOOLCHAIN="$(brew --prefix gcc-arm-embedded 2>/dev/null)/bin"
[ -d "$ARM_TOOLCHAIN" ] && export PATH="$ARM_TOOLCHAIN:$PATH"

# Convenience aliases
alias serial-port='ls /dev/tty.usb*'
alias flash='STM32_Programmer_CLI -c port=SWD -d build/KWS_TRON.elf -rst'
alias connect='screen $(ls /dev/tty.usb* | head -1) 115200'

echo "TRON KWS env active — Python: $(python3 --version), arm-gcc: $(arm-none-eabi-gcc --version 2>/dev/null | head -1 || echo 'not found')"
