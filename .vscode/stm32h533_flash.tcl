# stm32h533_flash.tcl — STM32H533 flash programmer for OpenOCD
# Programs flash via the non-secure FLASH controller (TrustZone disabled)
#
# STM32H533 FLASH controller (non-secure, base 0x40022000):
#   ACR     +0x00  Access control
#   NSKEYR  +0x04  Non-secure key register (write unlock sequence here)
#   NSSR    +0x20  Non-secure status (BSY=bit0, WBNE=bit1)
#   NSCR    +0x28  Non-secure control (LOCK=bit0, PG=bit1, SER=bit2, MER=bit3, SNB[5:0]=bits9:4, STRT=bit16)
#   NSCCR   +0x30  Non-secure clear control (clear error flags)
#
# Flash geometry: 512KB total, 64 sectors × 8KB, programmed in 16-byte (128-bit) rows.
# Flash base: 0x08000000

set FLASH_NSKEYR  0x40022004
set FLASH_NSSR    0x40022020
set FLASH_NSCR    0x40022028
set FLASH_NSCCR   0x40022030

set FLASH_KEY1    0x45670123
set FLASH_KEY2    0xCDEF89AB

# Status bits
set NSSR_BSY      0x00000001
set NSSR_WBNE     0x00000002
set NSSR_ERRORS   0x003F0000   ;# WRPERR|PGSERR|STRBERR|OBKERR|INCERR|OPTCHANGEERR

# Control bits
set NSCR_LOCK     0x00000001
set NSCR_PG       0x00000002
set NSCR_SER      0x00000004
set NSCR_MER      0x00000008
set NSCR_STRT     0x00010000

proc h533_wait_not_busy {} {
    global FLASH_NSSR NSSR_BSY NSSR_WBNE
    for {set i 0} {$i < 500000} {incr i} {
        set sr [mrw $FLASH_NSSR]
        if {($sr & ($NSSR_BSY | $NSSR_WBNE)) == 0} { return 0 }
    }
    error "STM32H533 flash: TIMEOUT waiting for ready (NSSR=0x[format %08x [mrw $FLASH_NSSR]])"
}

proc h533_clear_errors {} {
    global FLASH_NSCCR NSSR_ERRORS
    mww $FLASH_NSCCR $NSSR_ERRORS
}

proc h533_unlock {} {
    global FLASH_NSKEYR FLASH_KEY1 FLASH_KEY2 FLASH_NSCR NSCR_LOCK
    set cr [mrw $FLASH_NSCR]
    if {($cr & $NSCR_LOCK) == 0} { return }   ;# already unlocked
    mww $FLASH_NSKEYR $FLASH_KEY1
    mww $FLASH_NSKEYR $FLASH_KEY2
    set cr [mrw $FLASH_NSCR]
    if {($cr & $NSCR_LOCK) != 0} {
        error "STM32H533 flash: unlock FAILED"
    }
}

proc h533_mass_erase {} {
    global FLASH_NSCR NSCR_MER NSCR_STRT
    h533_wait_not_busy
    h533_unlock
    # MER + STRT in one write
    mww $FLASH_NSCR [expr {$NSCR_MER | $NSCR_STRT}]
    echo "  Mass erase in progress..."
    h533_wait_not_busy
    h533_clear_errors
    echo "  Mass erase done"
}

proc h533_sector_erase {sector} {
    global FLASH_NSCR NSCR_SER NSCR_STRT
    h533_wait_not_busy
    h533_unlock
    # SNB field = bits [9:4]
    set cr [expr {$NSCR_SER | (($sector & 0x3F) << 4) | $NSCR_STRT}]
    mww $FLASH_NSCR $cr
    h533_wait_not_busy
    h533_clear_errors
}

# Program a 16-byte (128-bit) row.
# addr must be 16-byte aligned; data is a list of 4 × 32-bit words (little-endian).
proc h533_write_row {addr w0 w1 w2 w3} {
    global FLASH_NSCR NSCR_PG NSSR_ERRORS FLASH_NSSR FLASH_NSCCR

    h533_wait_not_busy
    h533_clear_errors

    # Enable programming
    mmw $FLASH_NSCR $NSCR_PG 0

    # Write 4 words (16 bytes) to flash address
    mww $addr              $w0
    mww [expr {$addr + 4}] $w1
    mww [expr {$addr + 8}] $w2
    mww [expr {$addr + 12}] $w3

    # Wait for BSY to clear
    h533_wait_not_busy

    # Check for errors
    set sr [mrw $FLASH_NSSR]
    if {($sr & $NSSR_ERRORS) != 0} {
        # Clear PG first
        mmw $FLASH_NSCR 0 $NSCR_PG
        error "STM32H533 flash: write error at 0x[format %08x $addr]: NSSR=0x[format %08x $sr]"
    }

    # Disable PG
    mmw $FLASH_NSCR 0 $NSCR_PG
}

# Program a binary chunk. data_words is a TCL list of uint32 values.
# start_addr must be 16-byte aligned.
proc h533_program_words {start_addr data_words} {
    set n [llength $data_words]
    # Pad to multiple of 4 words (16 bytes)
    while {($n % 4) != 0} {
        lappend data_words 0xFFFFFFFF
        incr n
    }
    h533_unlock
    for {set i 0} {$i < $n} {incr i 4} {
        set addr [expr {$start_addr + $i * 4}]
        h533_write_row $addr \
            [lindex $data_words $i] \
            [lindex $data_words [expr {$i+1}]] \
            [lindex $data_words [expr {$i+2}]] \
            [lindex $data_words [expr {$i+3}]]
    }
}

# Top-level: erase and program a binary file to flash.
proc h533_flash_binary {bin_path flash_addr} {
    echo "STM32H533 flash: programming $bin_path to 0x[format %08x $flash_addr]"

    halt
    h533_wait_not_busy

    echo "Erasing flash..."
    h533_mass_erase
    h533_unlock

    echo "Programming..."
    # Use load_image to write to SRAM first, then we program via controller
    # Alternative: use fast_load_image + write via controller
    # For simplicity use load_image directly (OpenOCD will use the flash bank
    # write algorithm if one is registered, otherwise raw writes).
    # Since we pre-erased, raw writes to flash work in some configs.

    # Load image to RAM buffer at 0x20000000 and then copy via controller
    # Actually: use write_image which goes through mem_write → since no flash
    # driver is registered, it'll do raw AHB writes.
    # On STM32H5, raw AHB writes to flash address only work when:
    #   1. Flash is unlocked, PG bit set, and 16-byte aligned writes are done
    # OpenOCD's write_memory sends individual 32-bit writes which the
    # H5 FLASH controller accepts — it buffers them in an 8-word write buffer
    # (WBNE = write buffer not empty) and programs when full.

    # Enable PG globally for the duration
    h533_unlock
    mmw $::FLASH_NSCR $::NSCR_PG 0

    load_image $bin_path $flash_addr bin

    # Wait for any pending write
    h533_wait_not_busy
    $::FLASH_NSCCR

    # Disable PG
    mmw $::FLASH_NSCR 0 $::NSCR_PG
    h533_wait_not_busy

    echo "Verifying..."
    verify_image $bin_path $flash_addr bin

    echo "=== Programming COMPLETE ==="
    reset run
}

# Program an ELF file (preferred — handles multiple sections, VTOR, etc.)
proc h533_icache_disable {} {
    # ICACHE_CR (NS) = AHB1PERIPH_BASE_NS + 0x10400 = 0x40020000 + 0x10400 = 0x40030400
    # bit 0 = EN; write 0 to disable
    catch { mww 0x40030400 0x00000000 }
    after 2
}

proc h533_icache_invalidate_enable {} {
    # bit 2 = CACHEINV; trigger then re-enable
    catch { mww 0x40030400 0x00000004 }
    after 2
    catch { mww 0x40030400 0x00000001 }
}

proc h533_flash_elf {elf_path} {
    echo "STM32H533 flash: programming ELF $elf_path"

    # Use reset halt — brings CPU out of any fault/lockup state cleanly
    reset halt
    after 100

    # Disable ICache before flash operations (prevents cache coherency issues)
    h533_icache_disable

    h533_wait_not_busy
    h533_clear_errors

    echo "Erasing flash..."
    h533_mass_erase

    echo "Programming..."
    h533_unlock
    # Set PG bit
    mmw $::FLASH_NSCR $::NSCR_PG 0

    # Write all ELF sections to flash.
    # OpenOCD will do 32-bit writes; the H5 write buffer collects 4 writes (16 bytes)
    # then auto-programs. Works correctly as long as writes are to sequential addresses.
    load_image $elf_path

    # Force write buffer flush: if the last section wasn't a multiple of 16 bytes,
    # the write buffer holds partial data. Pad with 0xFFFFFFFF writes to fill it.
    # (0xFF is erased state so this is safe)
    # We can't easily know the last address here, so just wait for WBNE to clear.
    for {set i 0} {$i < 1000} {incr i} {
        set sr [mrw $::FLASH_NSSR]
        if {($sr & $::NSSR_WBNE) == 0} { break }
        after 1
    }

    h533_wait_not_busy

    # Disable PG
    mmw $::FLASH_NSCR 0 $::NSCR_PG

    # Re-enable and invalidate ICache so CPU sees new flash content
    h533_icache_invalidate_enable

    echo "Verifying..."
    verify_image $elf_path

    echo "=== Flash programming COMPLETE ==="
    reset run
}
