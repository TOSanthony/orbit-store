/* The install helper program, carried inside the service. */
    .section .rodata
    .global install_helper_elf
    .global install_helper_elf_end
    .balign 16
install_helper_elf:
    .incbin "build/install-helper.elf"
install_helper_elf_end:
    .byte 0
    .section .note.GNU-stack,"",@progbits
