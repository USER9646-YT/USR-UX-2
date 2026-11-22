.section .rodata
.global user_blob_start
.global user_blob_end

user_blob_start:
    call 1f
1:  pop %ecx                      /* ecx = runtime address of label 1 */
    add $(msg - 1b), %ecx         /* ecx = runtime address of msg     */

    mov $4, %eax                  /* sys_write */
    mov $1, %ebx                  /* fd 1      */
    mov $(msg_end - msg), %edx    /* length    */
    int $0x80

    mov $1, %eax                  /* sys_exit  */
    xor %ebx, %ebx                /* code 0    */
    int $0x80
2:  jmp 2b                        /* never reached */

msg:
    .ascii "Hello from user mode (ring 3)!\n"
msg_end:
user_blob_end:
