#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "elf.h"
#include "vfs.h"
#include "kheap.h"
#include "pmm.h"
#include "paging.h"
#include "kprintf.h"

#define USER_MIN     0x40000000u
#define USER_MAX     0x5F000000u          // the heap grows up to here, mmap and the stack are above
#define ELF_MAX_SIZE (8u * 1024u * 1024u)

#define PT_LOAD   1
#define PT_INTERP 3
#define PF_W      2
#define ET_EXEC   2
#define EM_386    3

struct elf32_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

_Static_assert(sizeof(struct elf32_ehdr) == 52, "elf header layout");
_Static_assert(sizeof(struct elf32_phdr) == 32, "program header layout");

static int read_file(const char *path, uint8_t **out, uint32_t *size) {
    struct vfs_stat st;
    int r = vfs_stat(path, &st);
    if (r) return r;
    if (st.type != VFS_FILE) return -EACCES;
    if (st.size < sizeof(struct elf32_ehdr) || st.size > ELF_MAX_SIZE) return -ENOEXEC;

    uint8_t *buf = kmalloc(st.size);
    if (!buf) return -ENOMEM;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { kfree(buf); return fd; }

    uint32_t got = 0;
    while (got < st.size) {
        int n = vfs_read(fd, buf + got, st.size - got);
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    vfs_close(fd);

    if (got != st.size) { kfree(buf); return -EIO; }

    *out  = buf;
    *size = st.size;
    return 0;
}

static int load_segment(uint32_t dir, const uint8_t *file, const struct elf32_phdr *ph) {
    uint32_t flags    = PTE_USER | ((ph->p_flags & PF_W) ? PTE_WRITE : 0);
    uint32_t start    = ph->p_vaddr & ~(PAGE_SIZE - 1);
    uint32_t end      = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t file_end = ph->p_vaddr + ph->p_filesz;

    for (uint32_t page = start; page < end; page += PAGE_SIZE) {
        if (paging_get_phys_in(dir, page)) {
            kprintf("exec: segments share page %08x (align them in the linker script)\n", page);
            return -ENOEXEC;
        }

        uint32_t frame = pmm_alloc();
        if (!frame) return -ENOMEM;
        memset((void *)frame, 0, PAGE_SIZE);

        uint32_t lo = page > ph->p_vaddr ? page : ph->p_vaddr;
        uint32_t hi = page + PAGE_SIZE < file_end ? page + PAGE_SIZE : file_end;
        if (hi > lo)
            memcpy((void *)(frame + (lo - page)),
                   file + ph->p_offset + (lo - ph->p_vaddr), hi - lo);

        if (paging_map_in(dir, page, frame, flags) != 0) { pmm_free(frame); return -ENOMEM; }
    }
    return 0;
}

int elf_load(const char *path, uint32_t dir, uint32_t *entry_out, uint32_t *brk_out) {
    uint8_t *file = NULL;
    uint32_t size = 0, image_end = 0;
    int r = read_file(path, &file, &size);
    if (r) return r;

    const struct elf32_ehdr *eh = (const struct elf32_ehdr *)file;

    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 1    || eh->e_ident[5] != 1   ||
        eh->e_type != ET_EXEC  || eh->e_machine != EM_386 ||
        eh->e_phentsize != sizeof(struct elf32_phdr) || eh->e_phnum == 0 ||
        eh->e_phoff > size ||
        (size - eh->e_phoff) / sizeof(struct elf32_phdr) < eh->e_phnum) {
        kprintf("exec: %s is not a static ELF32 i386 executable\n", path);
        r = -ENOEXEC;
        goto out;
    }

    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr *ph =
            (const struct elf32_phdr *)(file + eh->e_phoff + i * sizeof(*ph));

        if (ph->p_type == PT_INTERP) {             // a dynamically linked program: nothing to link it with
            kprintf("exec: %s is dynamically linked (build it with -static)\n", path);
            r = -ENOEXEC;
            goto out;
        }

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        if (ph->p_filesz > ph->p_memsz ||
            ph->p_offset > size || ph->p_filesz > size - ph->p_offset ||
            ph->p_vaddr < USER_MIN || ph->p_memsz > USER_MAX ||
            ph->p_vaddr > USER_MAX - ph->p_memsz) {
            kprintf("exec: bad segment %u in %s: vaddr %08x memsz %08x filesz %08x offset %08x\n",
                    i, path, ph->p_vaddr, ph->p_memsz, ph->p_filesz, ph->p_offset);
            kprintf("      (programs must load between %08x and %08x)\n", USER_MIN, USER_MAX);
            r = -ENOEXEC;
            goto out;
        }

        r = load_segment(dir, file, ph);
        if (r) goto out;

        if (ph->p_vaddr + ph->p_memsz > image_end) image_end = ph->p_vaddr + ph->p_memsz;
    }

    if (eh->e_entry < USER_MIN || eh->e_entry >= USER_MAX) {
        kprintf("exec: entry point %08x outside user space\n", eh->e_entry);
        r = -ENOEXEC;
        goto out;
    }

    *entry_out = eh->e_entry;
    *brk_out   = (image_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    r = 0;

out:
    kfree(file);
    return r;
}