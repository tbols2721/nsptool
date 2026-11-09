#!/usr/bin/env bash
# Stage the Ndless jailbreak on a connected TI-Nspire CX II.
#
# This only copies the installer files into the calculator's /ndless folder.
# Nothing runs until you open the installer document on the calculator, so
# you can stop at any point before that, or remove the files with --remove.
set -euo pipefail

NDLESS_TAG=r2022
NDLESS_URL=https://github.com/ndless-nspire/Ndless/releases/download/$NDLESS_TAG/ndless-$NDLESS_TAG.zip
NDLESS_SHA256=21853624cba88bd74204e45d42bfdf6d88d145dbb54534fe330cd4c2051e6f9a
INSTALLER=ndless_installer_4.5.5-6.2.0-6.4.0.tns
RESOURCES=ndless_resources.tns
# CX II OS versions this installer supports (Ndless r2022 README).
SUPPORTED_OS="6.2.0.333 6.4.0.74"

here=$(cd "$(dirname "$0")" && pwd)
nsptool=$here/nsptool
dl=$here/build/ndless-$NDLESS_TAG

usage() {
	echo "usage: $0 [--remove]"
	echo "  (no args)  download Ndless $NDLESS_TAG and copy its installer to /ndless"
	echo "  --remove   delete the staged Ndless files from the calculator"
}

case "${1:-}" in
	-h|--help) usage; exit 0 ;;
esac

[ -x "$nsptool" ] || { echo "nsptool not built; run ./build.sh first" >&2; exit 1; }

if [ "${1:-}" = "--remove" ]; then
	for f in $INSTALLER $RESOURCES; do
		"$nsptool" rm "/ndless/$f" || true
	done
	echo "Staged Ndless files removed. If Ndless was already run, reboot the"
	echo "calculator (reset hole on the back) to unload it."
	exit 0
elif [ $# -gt 0 ]; then
	usage
	exit 2
fi

# Check the calculator first so nothing is downloaded for an unsupported OS.
info=$("$nsptool" info)
echo "$info" | grep -E "Device name|OS version"
model=$(echo "$info" | sed -n 's/^Device name : //p')
os=$(echo "$info" | sed -n 's/^OS version  : //p')
case "$model" in
	"TI-Nspire CX II"*) ;;
	*) echo "This script only handles the CX II family (found: $model)." >&2; exit 1 ;;
esac
if ! echo " $SUPPORTED_OS " | grep -q " $os "; then
	echo "OS $os is not supported by Ndless $NDLESS_TAG (supported: $SUPPORTED_OS)." >&2
	exit 1
fi

# Download and verify against the pinned checksum.
mkdir -p "$dl"
zip=$dl/ndless-$NDLESS_TAG.zip
if [ ! -f "$zip" ]; then
	curl -fL --proto '=https' -o "$zip.part" "$NDLESS_URL"
	mv "$zip.part" "$zip"
fi
echo "$NDLESS_SHA256  $zip" | sha256sum -c --quiet || {
	echo "Checksum mismatch for $zip; refusing to continue." >&2
	rm -f "$zip"
	exit 1
}
unzip -oq "$zip" "$INSTALLER" "$RESOURCES" -d "$dl"

cat <<EOF

This will copy the Ndless $NDLESS_TAG installer into /ndless on the calculator.
Copying the files does not jailbreak it; that only happens if you later open
$INSTALLER on the calculator.

Before you do that, know that:
  - Ndless is unofficial and not supported by TI. Use it at your own risk.
  - It is unloaded by a reboot and must be re-run after each one.
  - Entering Press-to-Test (exam mode) reboots the calculator, unloading it.
  - The calculator's language must be English when you run the installer.
EOF
read -r -p "Type YES to copy the files: " answer
[ "$answer" = "YES" ] || { echo "Aborted."; exit 1; }

"$nsptool" mkdir /ndless
"$nsptool" put "$dl/$INSTALLER" "/ndless/$INSTALLER"
"$nsptool" put "$dl/$RESOURCES" "/ndless/$RESOURCES"

cat <<EOF

Files staged. Ndless is NOT installed yet. When you want to install it:
  1. On the calculator: Home > 5 Settings > 1 Change Language > English.
  2. Open My Documents > ndless > ${INSTALLER%.tns}.
  3. Press any letter or number key and wait. If it fails or reboots, try again.

To undo staging without installing: $0 --remove
EOF
