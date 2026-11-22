.section .text
.global _start
_start:
    xor %ebp, %ebp
    mov (%esp), %eax              /* argc */
    lea 4(%esp), %ecx             /* argv */
    lea 8(%esp,%eax,4), %edx      /* envp = argv + argc + 1 */
    push %edx
    push %ecx
    push %eax
    call __libc_start_main        /* never returns */
1:  hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
