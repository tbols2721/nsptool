#!/usr/bin/env bash
# Build nsptool against libnspire without autotools or root access.
# Fetches libnspire at a pinned commit and, if the libusb-1.0 development
# headers are not installed, unpacks them locally from the Debian package.
set -euo pipefail

LIBNSPIRE_REPO=https://github.com/Vogtinator/libnspire.git
LIBNSPIRE_COMMIT=7d7d962ca49ff9017f6f146336c51ef7f2edf4e0

here=$(cd "$(dirname "$0")" && pwd)
build=$here/build
mkdir -p "$build"

if [ ! -d "$build/libnspire" ]; then
	git clone "$LIBNSPIRE_REPO" "$build/libnspire"
fi
# Reset to the pinned commit and apply our fixes (see patches/).
git -C "$build/libnspire" reset -q --hard "$LIBNSPIRE_COMMIT"
for p in "$here"/patches/*.patch; do
	git -C "$build/libnspire" apply "$p"
done

# libusb headers: system copy if present, otherwise a local unpack.
if pkg-config --exists libusb-1.0; then
	usb_cflags=$(pkg-config --cflags libusb-1.0)
	usb_libs=$(pkg-config --libs libusb-1.0)
else
	if [ ! -f "$build/usbdev/usr/include/libusb-1.0/libusb.h" ]; then
		(cd "$build" && apt-get download libusb-1.0-0-dev)
		dpkg -x "$build"/libusb-1.0-0-dev_*.deb "$build/usbdev"
	fi
	usb_cflags="-I$build/usbdev/usr/include/libusb-1.0"
	ldconfig_bin=$(command -v ldconfig || true)
	if [ -z "$ldconfig_bin" ] && [ -x /usr/sbin/ldconfig ]; then
		ldconfig_bin=/usr/sbin/ldconfig
	fi
	[ -n "$ldconfig_bin" ] || { echo "ldconfig not found; install libusb development headers instead" >&2; exit 1; }
	usb_libs=$("$ldconfig_bin" -p | awk '/libusb-1.0.so.0 / && !p {p=$NF} END {print p}')
	[ -n "$usb_libs" ] || { echo "libusb-1.0 runtime library not found" >&2; exit 1; }
fi

# config.h is normally produced by ./configure; libnspire only needs the
# host byte order from it.
if gcc -dM -E - </dev/null | grep -q '__BYTE_ORDER__ __ORDER_BIG_ENDIAN__'; then
	echo '#define ENDIAN_BIG' > "$build/libnspire/config.h"
else
	echo '#define ENDIAN_LITTLE' > "$build/libnspire/config.h"
fi

L=$build/libnspire
inc="-I$L -I$L/src -I$L/src/api $usb_cflags -DHAVE_CONFIG_H"
objs=$build/obj
mkdir -p "$objs"
for f in "$L"/src/*.c "$L"/src/services/*.c; do
	gcc -O2 -w $inc -c "$f" -o "$objs/$(basename "${f%.c}").o"
done
g++ -O2 -w $inc -c "$L/src/cx2.cpp" -o "$objs/cx2.o"
for f in "$here"/*.c; do
	gcc -O2 -Wall $inc -c "$f" -o "$objs/tool_$(basename "${f%.c}").o"
done
g++ -o "$here/nsptool" "$objs"/*.o $usb_libs

echo "Built $here/nsptool"
