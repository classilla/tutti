# [Tutti II](https://www.floodgap.com/retrobits/tomy/xtomydev/tutti2.html): The Tape-Enabled Tomy Tutor Emulator

This is the development repository for Tutti II, basically the best Tomy Tutor emulator other than MAME (because, near as I can determine, it's the _only_ Tomy Tutor emulator other than MAME).

Tutti II runs on the following systems:

* Mac OS X 10.4 through 10.12 on PowerPC and `i386` (built with `gcc`)
* macOS 10.12-??? `x86_64` (built with `clang`)
* Windows 32-bit `i686`, tested on Windows 98 and Windows 10 (built with MinGW)

SDL 1.2.15 is required. See individual `Makefile`s for additional build and toolchain requirements. The Win32 port is compiled with [`mxe`](https://mxe.cc/).

Tutti II is offered under the New BSD License; see `LICENSE.txt`.

For usage and more information, see the [Tutti II homepage](https://www.floodgap.com/retrobits/tomy/xtomydev/tutti2.html).

### Beware the binary blobs

ROMs are included in `roms` but you are only legally entitled to use them if you are a current Tomy Tutor owner. The SDL library in `libs` is a special universal build for the 32-bit OS X port and is not required by the other platforms.

### Tutti _II_? Where's the first one?

The original Tutti was a Tomy Tutor simulator written [for the Commodore 64](https://www.floodgap.com/retrobits/tomy/tutti.html). Tutti II is a spiritual successor for modern systems with true emulation.
