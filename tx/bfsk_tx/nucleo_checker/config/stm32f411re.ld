ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : {
        *(.text*) *(.rodata*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx : {
        __exidx_start = .; *(.ARM.exidx*) __exidx_end = .;
    } > FLASH
    .preinit_array : {
        __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
    } > FLASH
    .init_array : {
        __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
    } > FLASH
    .fini_array : {
        __fini_array_start = .; KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .;
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4); _sdata = .;
        *(.data*) *(.RamFunc*)
        . = ALIGN(4); _edata = .;
    } > RAM AT> FLASH
    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .; __bss_start__ = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4); _ebss = .; __bss_end__ = .;
    } > RAM
    .heap_stack (NOLOAD) : {
        . = ALIGN(8); end = .; _end = .;
        . += 0x800; /* 최소 heap 2 KB */
        . += 0x2000; /* 최소 stack 8 KB */
    } > RAM
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
