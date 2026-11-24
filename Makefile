CC      := gcc
AS      := gcc
LD      := ld

CFLAGS  := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
           -fno-stack-protector -fno-pie -fno-pic -fno-builtin \
           -fno-tree-loop-distribute-patterns -nostdinc \
           -Iusr/include -Iusr/include/kernel -MMD -MP \
           -mno-sse -mno-sse2 -mno-mmx -mno-80387
ASFLAGS := -m32 -c
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

LIBGCC := $(shell $(CC) -m32 -print-libgcc-file-name)

LIBC_CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-builtin \
               -fno-stack-protector -fno-pie -fno-pic -fno-tree-loop-distribute-patterns \
               -mno-sse -mno-sse2 -mno-mmx -mfpmath=387 -nostdinc -Ilibc/include \
               -MMD -MP

LIBC_OBJS := $(patsubst %.c,%.o,$(wildcard libc/src/*.c)) libc/src/asm.o

USER_CFLAGS := -m32 -std=gnu11 -ffreestanding -nostdinc -O2 -Wall -Wextra \
               -fno-stack-protector -fno-pie -fno-pic -fno-builtin \
               -fno-tree-loop-distribute-patterns \
               -mno-sse -mno-sse2 -mno-mmx -mno-80387 -Iusr/include -Iuser/
USER_LDFLAGS := -m elf_i386 -T user/user.ld -nostdlib \
                -z max-page-size=4096 -z noexecstack

APP_CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-pie -fno-pic \
              -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx -mfpmath=387 \
              -nostdinc -Ilibc/include
APP_LDFLAGS := -m elf_i386 -T user/user.ld -nostdlib -z max-page-size=4096 -z noexecstack

USER_BINS := rootfs/bin/hello rootfs/bin/echo rootfs/bin/fush rootfs/bin/crash \
             rootfs/bin/ls rootfs/bin/cat rootfs/bin/login rootfs/bin/whoami \
             rootfs/bin/id rootfs/bin/chmod rootfs/bin/chown rootfs/bin/sha256 \
             rootfs/bin/cp rootfs/bin/mv rootfs/bin/rm rootfs/bin/mkdir \
             rootfs/bin/rmdir rootfs/bin/touch rootfs/bin/grep rootfs/bin/uname \
             rootfs/bin/vi rootfs/bin/clear rootfs/bin/sleep rootfs/bin/kill \
             rootfs/bin/pkill rootfs/bin/ps rootfs/bin/wc rootfs/bin/head \
             rootfs/bin/tail rootfs/bin/tee rootfs/bin/true rootfs/bin/false \
             rootfs/bin/sync rootfs/bin/fbtest rootfs/bin/mousetest \
             rootfs/bin/hostname rootfs/bin/evtest rootfs/bin/su rootfs/bin/sudo \
             rootfs/bin/env rootfs/sbin/killinit \
             rootfs/sbin/init rootfs/sbin/halt rootfs/sbin/poweroff \
             rootfs/sbin/reboot rootfs/sbin/telinit rootfs/sbin/runlevel \
             rootfs/bin/sh rootfs/bin/cfetch rootfs/bin/uptime \
             rootfs/bin/free

# every program in apps/ (built against the libc) goes into the image automatically
APP_BINS := $(patsubst apps/%.c,rootfs/bin/%,$(wildcard apps/*.c))

OBJS := boot/boot.o boot/gdt.o boot/isr.o boot/switch.o \
        kernel/kernel.o kernel/terminal.o kernel/kprintf.o kernel/string.o \
        kernel/gdt.o kernel/idt.o kernel/pic.o kernel/pit.o kernel/keyboard.o \
        kernel/pmm.o kernel/paging.o kernel/kheap.o kernel/ata.o kernel/part.o \
        kernel/pci.o kernel/ahci.o kernel/ehci.o kernel/usbstor.o \
        kernel/vfs.o kernel/ramfs.o kernel/devfs.o kernel/pipe.o kernel/ext2.o \
        kernel/syscall.o kernel/elf.o kernel/proc.o kernel/tty.o kernel/power.o \
        kernel/fb.o kernel/fbdev.o kernel/mouse.o kernel/hostname.o \
        kernel/sys_init.o kernel/sys_extra.o kernel/poll.o \
        kernel/sys_socket.o kernel/pty.o kernel/blkdev.o \
        kernel/panic.o kernel/rtc.o kernel/sys_cred.o kernel/sys_tty.o \
        kernel/sys_mem.o kernel/sys_time.o kernel/sys_misc.o \
        kernel/procfs.o kernel/keymap.o kernel/latin1.o kernel/kbdmenu.o

KERNEL := usr-ux-2.bin

# extra kernel command line for "make run", e.g.  make run APPEND="kbd=de"
APPEND ?=

all: $(KERNEL)

# ---------- the kernel ----------

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@grub-file --is-x86-multiboot $@ && echo "Multiboot OK"

boot/%.o: boot/%.S
	$(AS) $(ASFLAGS) $< -o $@

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---------- user programs (inline syscall wrappers, no libc) ----------

user/crt0.o: user/crt0.s
	$(AS) $(ASFLAGS) $< -o $@

user/%.o: user/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

rootfs/sbin/%: user/%.o user/crt0.o user/user.ld
	mkdir -p rootfs/sbin
	$(LD) $(USER_LDFLAGS) -o $@ user/crt0.o $<

rootfs/bin/%: user/%.o user/crt0.o user/user.ld
	mkdir -p rootfs/bin
	$(LD) $(USER_LDFLAGS) -o $@ user/crt0.o $<

# Only needed if your shell source is user/ush.c but everything else expects /bin/sh:
#rootfs/bin/sh: rootfs/bin/ush
#	cp $< $@

# ---------- the libc and the programs built against it ----------

libc/src/%.o: libc/src/%.c libc/src/internal.h
	$(CC) $(LIBC_CFLAGS) -c $< -o $@

libc/src/%.o: libc/src/%.S
	$(CC) -m32 -c $< -o $@

libc/crt0.o: libc/crt0.S
	$(CC) -m32 -c $< -o $@

libc/libc.a: $(LIBC_OBJS)
	rm -f $@
	ar rcs $@ $(LIBC_OBJS)

libc: libc/libc.a libc/crt0.o

apps/%.o: apps/%.c
	$(CC) $(APP_CFLAGS) -c $< -o $@

rootfs/bin/%: apps/%.o libc/crt0.o libc/libc.a user/user.ld
	mkdir -p rootfs/bin
	$(LD) $(APP_LDFLAGS) -o $@ libc/crt0.o $< libc/libc.a $(LIBGCC)

# ---- tcc ----
TCC_DIR    := ports/tcc
TCC_CFLAGS := $(APP_CFLAGS) -w -DHAVE_CONFIG_H=1 -I$(TCC_DIR)

$(TCC_DIR)/tcc.o: $(TCC_DIR)/tcc.c $(TCC_DIR)/config.h
	$(CC) $(TCC_CFLAGS) -c $< -o $@

$(TCC_DIR)/tcc: $(TCC_DIR)/tcc.o libc/crt0.o libc/libc.a user/user.ld
	$(LD) $(APP_LDFLAGS) -o $@ libc/crt0.o $< libc/libc.a $(LIBGCC)

TCC_ALLOCA := $(firstword $(wildcard $(TCC_DIR)/lib/alloca86.S $(TCC_DIR)/lib/alloca.S))

# tcc's runtime helpers (64-bit division etc.), built with gcc
$(TCC_DIR)/libtcc1.a: $(TCC_DIR)/lib/libtcc1.c $(TCC_ALLOCA)
	$(CC) -m32 -O2 -fno-pie -fno-stack-protector -nostdinc \
	      -I$(TCC_DIR)/include -c $(TCC_DIR)/lib/libtcc1.c -o $(TCC_DIR)/lib/libtcc1.o
	$(CC) -m32 -c $(TCC_ALLOCA) -o $(TCC_DIR)/lib/alloca.o
	rm -f $@
	ar rcs $@ $(TCC_DIR)/lib/libtcc1.o $(TCC_DIR)/lib/alloca.o

# ---- the development environment on the disk ----
sysroot: libc $(TCC_DIR)/tcc $(TCC_DIR)/libtcc1.a
	mkdir -p rootfs/bin rootfs/lib/tcc/include rootfs/usr
	cp $(TCC_DIR)/tcc rootfs/bin/tcc
	cp libc/libc.a libc/crt0.o rootfs/lib/
	cp libc/crt0.o rootfs/lib/crt1.o
	$(CC) -m32 -c -x assembler /dev/null -o rootfs/lib/crti.o
	$(CC) -m32 -c -x assembler /dev/null -o rootfs/lib/crtn.o
	cp $(TCC_DIR)/libtcc1.a rootfs/lib/
	cp $(TCC_DIR)/include/*.h rootfs/lib/tcc/include/
	rm -rf rootfs/usr/include
	cp -r libc/include rootfs/usr/include

# ---- Nano-X (ports/nanox) ----
# The port builds with its own Makefiles; ports/nanox/src/Arch.rules has a USR/UX block (selected
# by USRUX = Y in ports/nanox/src/config) that supplies this OS's compiler and linker flags.
# It needs libc/libc.a and libc/crt0.o. The demos are built with -k (keep going) because some of
# them can't work here yet (nxterm and tmterm need pseudo-terminals); the final cp fails
# if demo-hello itself was not built. Run "make nanox" and then "make disk".
NANOX_SRC := ports/nanox/src

# demo programs that build and make sense on this OS. Programs that need pseudo-terminals,
# sockets or other missing pieces are left out; a program that did not build is skipped.
NANOX_APPS := demo-hello nxtetris nxclock nxeyes nxroach nxmag nxmine nxworld nxview nxterm

nanox: libc
	$(MAKE) -C $(NANOX_SRC)/nanox
	-$(MAKE) -k -C $(NANOX_SRC)/demos/nanox
	mkdir -p rootfs/bin
	cp $(NANOX_SRC)/bin/demo-hello rootfs/bin/
	-cp $(NANOX_SRC)/bin/nano-X rootfs/bin/
	for a in $(NANOX_APPS); do if [ -f $(NANOX_SRC)/bin/$$a ]; then cp $(NANOX_SRC)/bin/$$a rootfs/bin/; echo "installed $$a"; fi; done

nanox-clean:
	$(MAKE) -C $(NANOX_SRC) clean

# ---------- images and emulator ----------

# builds the single bootable disk: MBR + GRUB + ext2 partition (asks for sudo)
disk: $(KERNEL) $(USER_BINS) $(APP_BINS) sysroot
	KERNEL=$(KERNEL) sh tools/mkimage.sh

# quick test loop: QEMU loads the kernel itself (text console), the root filesystem comes from disk.img
run: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL) $(if $(APPEND),-append "$(APPEND)") -drive file=disk.img,format=raw,if=ide

# boots disk.img like a real PC: BIOS -> GRUB -> /boot/kernel.bin (framebuffer console)
bootsys:
	qemu-system-i386 -drive file=disk.img,format=raw,if=ide

# old kernel-only ISO (no root filesystem): superseded by "disk"
iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/$(KERNEL)
	printf 'menuentry "USR/UX 2" {\n\tmultiboot /boot/$(KERNEL)\n}\n' > isodir/boot/grub/grub.cfg
	grub-mkrescue -o usr-ux-2.iso isodir

# "make clean" removes this OS's own build products. The Nano-X objects are left alone
# (see nanox-clean), and so are rootfs/lib/modules and the rest of rootfs/lib.
clean:
	rm -rf $(OBJS) $(OBJS:.o=.d) $(KERNEL) isodir usr-ux-2.iso \
	       user/*.o apps/*.o libc/src/*.o libc/src/*.d libc/crt0.o libc/libc.a \
	       rootfs/bin/* rootfs/sbin/* rootfs/boot/kernel.bin \
	       $(TCC_DIR)/tcc.o $(TCC_DIR)/tcc.d $(TCC_DIR)/tcc $(TCC_DIR)/libtcc1.a $(TCC_DIR)/lib/*.o \
	       rootfs/lib/*.a rootfs/lib/*.o rootfs/lib/tcc rootfs/usr/include
	       touch rootfs/bin/f rootfs/sbin/f

.PHONY: all run iso clean disk bootsys libc sysroot nanox nanox-clean

-include $(OBJS:.o=.d)
-include $(LIBC_OBJS:.o=.d)
-include $(TCC_DIR)/tcc.d
