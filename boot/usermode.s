.section .text
.global enter_usermode
.type enter_usermode, @function
/* void enter_usermode(uint32_t eip, uint32_t esp) */
enter_usermode:
    mov 4(%esp), %eax        /* user eip */
    mov 8(%esp), %ecx        /* user esp */

    mov $0x23, %dx           /* user data selector, RPL 3 */
    mov %dx, %ds
    mov %dx, %es
    mov %dx, %fs
    mov %dx, %gs

    push $0x23               /* ss     */
    push %ecx                /* esp    */
    pushf
    pop %edx
    or $0x200, %edx          /* make sure IF is set */
    push %edx                /* eflags */
    push $0x1B               /* cs: user code selector, RPL 3 */
    push %eax                /* eip    */
    iret
