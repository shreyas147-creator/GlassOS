ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x200000;
 .text : { __text_start = .; *(.text.prologue) *(.text*) __text_end = .; } :text
 . = 0x280000;
 .rodata : { __rodata_start = .; *(.rodata*) __rodata_end = .; } :rodata
 . = 0x290000;
 .data : { __data_start = .; *(.data*) __data_end = .; } :data
 .bss : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; } :data
 ASSERT(__text_start == 0x200000, "loader expects text at 2MiB")
 ASSERT(__rodata_start == 0x280000, "loader expects rodata at 0x280000")
 ASSERT(__data_start == 0x290000, "loader expects writable payload at 0x290000")
 ASSERT(. <= 0x300000, "payload exceeds bootstrap mapping")
}
