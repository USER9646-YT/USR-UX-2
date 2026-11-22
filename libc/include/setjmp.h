#ifndef _SETJMP_H
#define _SETJMP_H
typedef int jmp_buf[6];
typedef jmp_buf sigjmp_buf;
int  setjmp(jmp_buf) __attribute__((returns_twice));
void longjmp(jmp_buf, int) __attribute__((noreturn));
#define sigsetjmp(env, save) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)
#endif