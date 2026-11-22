.section .text
.global gdt_flush
.type gdt_flush, @function
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)

    mov $0x10, %ax          /* kernel data selector */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    ljmp $0x08, $.reload_cs /* far jump reloads CS with kernel code selector */
.reload_cs:
    ret