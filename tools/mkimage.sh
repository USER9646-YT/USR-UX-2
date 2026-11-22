#!/bin/sh
# Builds disk.img: MBR + GRUB + one ext2 partition (1 MiB offset) containing
# /boot/kernel.bin, /boot/grub/... and the root filesystem from rootfs/.
# Needs sudo, losetup, sfdisk, mke2fs and grub-install (Debian/Ubuntu: grub-pc-bin).
# Run from the project root:  sh tools/mkdisk.sh
set -e

KERNEL=${KERNEL:-usr-ux-2.bin}
IMG=disk.img
SIZE_MB=${SIZE_MB:-1024}

GRUB_INSTALL=$(command -v grub-install || command -v grub2-install || true)
[ -n "$GRUB_INSTALL" ] || { echo "grub-install not found (package grub-pc-bin)"; exit 1; }
[ -f "$KERNEL" ] || { echo "$KERNEL has not been built yet (run make)"; exit 1; }

# ---- the source tree: layout, kernel, grub.cfg, permissions ----
mkdir -p rootfs/dev rootfs/tmp rootfs/run rootfs/var rootfs/root rootfs/home/user \
         rootfs/lib/modules rootfs/boot/grub
cp "$KERNEL" rootfs/boot/kernel.bin
cp tools/grub.cfg rootfs/boot/grub/grub.cfg

find rootfs -type d -exec chmod 755 {} +
chmod 755 rootfs/bin/* rootfs/sbin/* 2>/dev/null || true
chmod 700 rootfs/root
chmod 1777 rootfs/tmp
chmod 644 rootfs/etc/passwd rootfs/etc/inittab rootfs/etc/rc* rootfs/etc/hostname 2>/dev/null || true
chmod 600 rootfs/etc/shadow
chmod 440 rootfs/etc/sudoers 2>/dev/null || true

# ---- the disk: partition table ----
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
printf 'label: dos\nstart=2048, type=83, bootable\n' | sfdisk -q "$IMG"

# ---- the partition: filesystem, files, GRUB ----
MNT=$(mktemp -d)
LOOP=$(sudo losetup --find --show --partscan "$IMG")

cleanup() {
    sudo umount "$MNT" 2>/dev/null || true
    sudo losetup -d "$LOOP" 2>/dev/null || true
    rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

sudo partprobe "$LOOP" 2>/dev/null || true
for i in 1 2 3 4 5 6 7 8 9 10; do
    [ -b "${LOOP}p1" ] && break
    sleep 0.3
done
[ -b "${LOOP}p1" ] || { echo "partition device ${LOOP}p1 did not appear"; exit 1; }

sudo mke2fs -q -t ext2 -b 1024 -L usrux "${LOOP}p1"
sudo mount "${LOOP}p1" "$MNT"

sudo cp -a rootfs/. "$MNT"/
sudo chown -R 0:0 "$MNT"
sudo chown 1000:1000 "$MNT/home/user"

# setuid bits last: chown clears them
sudo chmod 4755 "$MNT/bin/su" "$MNT/bin/sudo" 2>/dev/null || echo "note: su/sudo are not built yet"

sudo "$GRUB_INSTALL" --target=i386-pc --boot-directory="$MNT/boot" \
                     --modules="part_msdos ext2" --no-floppy "$LOOP"

sync
echo "disk.img is ready"
