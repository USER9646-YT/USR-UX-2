.section .text
.global switch_context
.type switch_context, @function
/* void switch_context(uint32_t *old_ksp, uint32_t new_ksp) */
switch_context:
    mov 4(%esp), %eax        /* where to save the old stack pointer */
    mov 8(%esp), %edx        /* new stack pointer                   */
    push %ebp
    push %ebx
    push %esi
    push %edi
    sub $108, %esp
    fnsave (%esp)            /* the old process's x87 state */
    mov %esp, (%eax)
    mov %edx, %esp
    frstor (%esp)            /* the new process's x87 state */
    add $108, %esp
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret
