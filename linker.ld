ENTRY(_start)

SECTIONS
{
    . = 0xF000;

    .text : AT(0xF000) {
        *(.text.prologue)   /* Force our entry code to byte index 0 of the binary! */
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
}