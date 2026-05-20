/*
 * STM32H533RETX_FLASH.ld
 * Linker script for STM32H533RETx — NUCLEO-H533RE
 *
 * Memory map (from STM32H533 reference manual RM0481 + BSP2 sysdef.h):
 *   Flash  0x08000000  512 KB
 *   SRAM1  0x20000000  256 KB  \
 *   SRAM2  0x20040000   16 KB   > contiguous — 272 KB total (0x44000)
 *   SRAM3  (none on H533)      /
 *
 * Sections layout:
 *   FLASH → .isr_vector .text .rodata .ARM .preinit_array .init_array .fini_array
 *   RAM   → .data (LMA in flash, VMA in RAM) .bss .mtk_bsp2 ._user_heap_stack
 */

ENTRY(Reset_Handler)

_Min_Heap_Size  = 0x800;   /* 2 KB  — increase if malloc is used */
_Min_Stack_Size = 0x1000;  /* 4 KB  — µT-Kernel tasks get their own stacks */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 272K
}

SECTIONS
{
  /* ── Interrupt vector table — must be first in flash ─────────────────── */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  /* ── µT-Kernel exception table (BSP2 places it at start of RAM) ────────
   * The BSP2 mtkernel.ld appends .mtk_exctbl and marks __mtk3_SYSMEM_START.
   * We satisfy that here so mtkernel.ld (included via BSP build) can overlay.
   */
  .mtk_exctbl (NOLOAD) :
  {
    . = ALIGN(1024);      /* EXCTBL_ALIGN from sysdef.h */
    *(.mtk_exctbl)
    __mtk3_SYSMEM_START = .;
  } >RAM

  /* ── Code and read-only data ────────────────────────────────────────────── */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM        : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* ── Initialised data — load from flash, run from RAM ─────────────────── */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  /* ── Zero-initialised data ────────────────────────────────────────────── */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* ── DMA circular audio buffer — place in SRAM2 (non-cacheable region) ──
   * Tag your DMA buffer: __attribute__((section(".dma_buf")))
   */
  .dma_buf (NOLOAD) :
  {
    . = ALIGN(4);
    *(.dma_buf)
    . = ALIGN(4);
  } >RAM

  /* ── Heap and stack size check ────────────────────────────────────────── */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* ── Discard C++ exception handling (unused on bare-metal) ────────────── */
  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
