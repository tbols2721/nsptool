# nsptool

A Linux command-line tool for TI-Nspire calculators over USB. It reads device
information, transfers documents, backs up My Documents, and checks OS packages
before sending an update. USB communication uses
[libnspire](https://github.com/Vogtinator/libnspire).

## Requirements and build

Use Linux with GCC, G++, Git, tar, patch, pkg-config, and the libusb 1.0 runtime. The build
uses libnspire revision `7d7d962ca49ff9017f6f146336c51ef7f2edf4e0` and applies
the repository's bounded-read patch before compiling.

On Debian or Ubuntu, the normal development dependencies are:

```sh
sudo apt install build-essential git tar patch pkg-config libusb-1.0-0-dev
```

Clone and build:

```sh
git clone https://github.com/tbols2721/nsptool.git
cd nsptool
./build.sh
./nsptool --help
```

If libusb development headers are absent, `build.sh` can download and unpack
the Debian development package under ignored `build/`, without installing it
system-wide. This fallback needs `apt-get`, `dpkg`, and a discoverable libusb
runtime. It is specific to Debian-style systems. Preparation needs network
access; no calculator is required to compile or display help.

The build directory contains a pristine upstream Git cache. Each build extracts
the pinned revision into a separate source directory and applies the patches
there. Repeated builds leave the Git cache clean. If the cache has local edits,
the build stops so you can preserve them.

## Commands

Connect the intended calculator and ensure your user can access its USB device.
TI calculators use USB vendor ID `0451`. Distribution udev rules, including
those shipped with TiLP packages, can grant normal-user access; check your
system's rule and session policy before using elevated privileges.

| Command | Effect |
| --- | --- |
| `./nsptool info` | Read model, OS/boot versions, electronic ID, and storage |
| `./nsptool ls /` | Read My Documents directory entries |
| `./nsptool backup` | Copy My Documents into a new timestamped local directory |
| `./nsptool backup /path/to/new-backup` | Copy into the specified new directory |
| `./nsptool get /folder/example.tns example.tns` | Read a calculator file and write the local target |
| `./nsptool put example.tns /folder/example.tns` | Write a document to the calculator |
| `./nsptool mkdir /folder` | Create a calculator directory |
| `./nsptool rm /folder/example.tns` | Delete a calculator file |
| `./nsptool osinstall TI-Nspire.tcc2` | Check and send an OS package after confirmation |

`get` can overwrite its local target. `put` and `rm` change calculator files
without a separate confirmation prompt. A backup reports errors and returns
nonzero if any file fails; inspect the result before treating it as complete.
Device information and directory listings can contain identifying data. Read
[PRIVACY.md](PRIVACY.md) before sharing output or backups.

## OS package checks

Back up documents before an OS update. Choose a package from an appropriate
vendor source for the exact calculator model. `osinstall` refuses a mismatched
file extension, a failed CX II package-content check, or an older package version.
For supported CX II ZIP packages, stored files must appear in `manifest.txt`
and match their recorded SHA-256 hashes.

The tool does not authenticate the manifest signature; package integrity is
separate from vendor authenticity. Older non-ZIP formats do not receive the
same content checks. The calculator's own installation checks still apply.

The transfer asks for confirmation. Keep the USB cable connected through
installation and reboot. There is no rollback facility in this tool. OS images
are not included in this repository.

## Optional Ndless staging

`ndless.sh` is a separate helper pinned to
[Ndless r2022](https://github.com/ndless-nspire/Ndless/releases/tag/r2022).
It accepts CX II OS versions `6.2.0.333` and `6.4.0.74`, verifies the downloaded
archive against a pinned checksum, and asks before copying the installer and
resources into `/ndless`.

```sh
./ndless.sh --help
./ndless.sh
./ndless.sh --remove
```

This helper also needs `curl` and `unzip`. Copying files stages an installer;
installation requires opening that document on the calculator. Follow the
pinned release's instructions and supported-version limits. `--remove` deletes
the staged files; it does not undo an installed OS update. This project is
independent of TI and the Ndless project.

## Validation and limits

```sh
./build.sh
python3 -m unittest discover -s tests -v
```

The tests compare SHA-256 against Python's standard library, validate synthetic
package metadata, reject changed/missing/unlisted payloads and compressed
archives, distinguish non-packages, and exercise help without USB access.
They use no vendor OS images or personal documents.

The public edition builds on Debian 13 amd64. Historical hardware testing
covered a TI-Nspire CX II CAS on Ubuntu arm64. USB transfers, backups, and OS
installation are not exercised by the automated tests. Other models and OS
versions require separate hardware validation.

## Credits and license

[libnspire](https://github.com/Vogtinator/libnspire) provides the USB protocol
implementation. The local patch bounds copies in `nspire_file_read()` when a
remote file grows between size queries and reads. Downloaded upstream source
retains its notices and stays under `build/`.

[Ndless](https://github.com/ndless-nspire/Ndless) supplies its optional installer;
this repository does not redistribute that release or TI OS packages.
Project source is licensed under [GNU GPL version 3](LICENSE), consistent with
the linked libnspire code. Product names identify compatible hardware and do
not imply vendor endorsement.
