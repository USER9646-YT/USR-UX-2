.text

/* long __syscall6(n, a, b, c, d, e, f) */
.global __syscall6
.type __syscall6, @function
__syscall6:
    push %ebp
    push %ebx
    push %esi
    push %edi
    mov 20(%esp), %eax
    mov 24(%esp), %ebx
    mov 28(%esp), %ecx
    mov 32(%esp), %edx
    mov 36(%esp), %esi
    mov 40(%esp), %edi
    mov 44(%esp), %ebp
    int $0x80
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

/* int setjmp(jmp_buf) */
.global setjmp
.global _setjmp
.type setjmp, @function
setjmp:
_setjmp:
    mov 4(%esp), %eax
    mov %ebx, 0(%eax)
    mov %esi, 4(%eax)
    mov %edi, 8(%eax)
    mov %ebp, 12(%eax)
    lea 4(%esp), %ecx            /* esp after the return */
    mov %ecx, 16(%eax)
    mov (%esp), %ecx             /* return address */
    mov %ecx, 20(%eax)
    xor %eax, %eax
    ret

/* void longjmp(jmp_buf, int) */
.global longjmp
.global _longjmp
.type longjmp, @function
longjmp:
_longjmp:
    mov 4(%esp), %edx
    mov 8(%esp), %eax
    test %eax, %eax
    jnz 1f
    inc %eax
1:  mov 0(%edx), %ebx
    mov 4(%edx), %esi
    mov 8(%edx), %edi
    mov 12(%edx), %ebp
    mov 16(%edx), %esp
    jmp *20(%edx)

.section .note.GNU-stack,"",@progbits
