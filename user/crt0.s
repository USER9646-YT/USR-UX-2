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
    call main
    mov %eax, %ebx                /* exit code = main's return value */
    mov $1, %eax                  /* sys_exit */
    int $0x80
1:  jmp 1b

.section .note.GNU-stack,"",@progbits
