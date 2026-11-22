#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdint.h>

//   Loads the static ELF32 executable at `path` into the address space `dir`
//   (physical address of a page directory). Returns 0 and the entry point,
//   or -errno.
// int elf_load(const char *path, uint32_t dir, uint32_t *entry);
int elf_load(const char *path, uint32_t dir, uint32_t *entry, uint32_t *brk);

#endif