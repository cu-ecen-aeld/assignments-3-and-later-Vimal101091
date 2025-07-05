#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here

      make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
   make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
  make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
  make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
  make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"

cp ${OUTDIR}/linux-stable/arch/arm64/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories

mkdir -p ${OUTDIR}/rootfs
cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib proc sbin sys tmp usr var
mkdir -p usr/bin usr/sbin
mkdir -p var/log lib64

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox

    make distclean
    make defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
else
    cd busybox
fi

# TODO: Make and install busybox
 make CONFIG_PREFIX=${OUTDIR}/rootfs  ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

 cd ${OUTDIR}/rootfs

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
mkdir -p ${OUTDIR}/rootfs/lib
mkdir -p  ${OUTDIR}/rootfs/lib64

sudo cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 lib
sudo cp ${SYSROOT}/lib64/libm.so.6 lib64
sudo cp  ${SYSROOT}/lib64/libresolv.so.2 lib64
sudo cp  ${SYSROOT}/lib64/libc.so.6 lib64


# TODO: Make device nodes

mkdir -p ${OUTDIR}/rootfs/dev
cd ${OUTDIR}/rootfs
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 622 ${OUTDIR}/rootfs/dev/console c 5 1

# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make CROSS_COMPILE=${CROSS_COMPILE} clean

make CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir -p ${OUTDIR}/rootfs/home/conf

 cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home
 cp  ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home
 cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home
 cp ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home
 cp ${FINDER_APP_DIR}/writer.c ${OUTDIR}/rootfs/home
 cp ${FINDER_APP_DIR}/conf/username.txt ${OUTDIR}/rootfs/home/conf
 cp ${FINDER_APP_DIR}/conf/assignment.txt ${OUTDIR}/rootfs/home/conf
 
 
 
 chmod +x ${OUTDIR}/rootfs/home/finder.sh
 chmod +x ${OUTDIR}/rootfs/home/finder-test.sh
 chmod +x ${OUTDIR}/rootfs/home/autorun-qemu.sh
 chmod +x ${OUTDIR}/rootfs/home/writer

# TODO: Chown the root directory

cd ${OUTDIR}/rootfs
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root | gzip > ${OUTDIR}/initramfs.cpio.gz
