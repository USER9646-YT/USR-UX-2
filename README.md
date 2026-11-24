# USR/UX 2 - a small UNIX-like operating system
USR/UX 2 (UserUNIX) is a UNIX clone made for 32-bit x86 computers written from scratch. It tries to be more POSIX compliant than its predecessor, but it wont be as professional as other OsDev projects like Zinnia, Ethereal or Astral. It can run on 4 MB of RAM.

It is not the same as the first version, which is based on TanjaOS. This is a fully independent OS not based off anything. 

Its file system is ext2.

Default password for root is: root. For the user "user", the password is: aaa.

Also delete all files titled: "t" in the directories of the rootfs folder before compiling

## What is needed?

```
Specs needed for USR/UX 2 to work:
Storage:         ATA/IDE HDD or a SATA SSD/HDD on ATA/Compatibility mode. SATA/AHCI is done for QEMU but not for bare metal usage.
Memory:          4 MB of RAM minimum, for Nano-X/Microwindows or compiling, 6-12 mb or more.
Processor:       586/Pentium up to Pentium III minimum, Pentium 4 recommended
Video card:      Any VGA card can work with that mistake of a UNIX. Yes, VGA, EGA could work but the frame buffer would not like it very much (HDMI/DP probably works too, didnt test it tho).
Minimum storage: 32 mb. Thats all. Disk size is set at 1 GB but you can change it in mkimage.sh to be 32 mb
Computer:        early-late 90s minimum (storage issues MAY happen), recommended early-mid 2000s (SATA on compatibility mode)
Boot type:       MBR BIOS, which means EFI GPT does not work.
```

## Applications
Init: 

/sbin/init - SysV style Init

Its shells:

fush - Fast and Usable Shell (/bin/fush and /bin/sh) - Minimal shell system

lsh - Stephen Brennan's LSH (/bin/lsh).

Its C library:

ulibc - UserLibraryC/User C Library (libc/ in source code)

## What you will need to do to install:
You will need:
```bash
e2fsprogs, xorriso, grub-mkrescue, gcc, nasm, ld
```
Now, to compile it, you will need to do:

```bash
make libc    - Makes the C library
make nanox   - Makes NanoX
make         - Makes the kernel
make disk    - Makes the binaries in user/ and apps/ and then the ext2 rootfs disk (bootable)
make run     - Runs the kernel binary with rootfs as its storage (no FB!)
make bootsys - Runs the img file (necessary for framebuffer to work!)
```
To start the GUI, you will need to do (inside the OS):
```sh
startnx
```

# Images

<img width="1023" height="765" alt="usruxscreen" src="https://github.com/user-attachments/assets/96b9d18b-4013-41e9-9074-bf3c48206432" />
<img width="1023" height="765" alt="nxterm-nxclock" src="https://github.com/user-attachments/assets/f2d50bf4-e30a-4ea0-b709-ca13b5810dce" />

# Code used from other people:

https://github.com/ss7m/paleofetch
Copyright (c) 2020. Sam Barr - MIT License

https://github.com/brenns10/lsh
Under the Unlicense

https://github.com/thepanoc95/cfetch
Copyright (c) 2026, OakyMacintosh - BSD Clause 3

https://github.com/tinycc/tinycc
GNU Lesser General Public License

https://github.com/ghaerr/microwindows
GNU General Public License V2

See "THIRDPARTYLICENSE" for more information.

# What is missing?
Vital disk commands such as mount, umount and dd

Fat32 compatibility

Sound

Shortcuts/Links
