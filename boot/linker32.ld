ENTRY(multiboot_entry)
SECTIONS {
 . = 0x100000;
 .multiboot : { KEEP(*(.multiboot)) }
 .text : { *(.text*) }
 .data : { *(.data*) *(.rodata*) }
 .kernel 0x200000 : {
  __payload_start = .;
  *(.kernel)
  __payload_end = .;
 }
 ASSERT(__payload_start == 0x200000, "payload start must match loader page tables")
 ASSERT(__payload_end <= 0x300000, "payload exceeds loader page table window")
}
