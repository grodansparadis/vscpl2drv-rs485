# vscpl2drv-rs485

![License](https://img.shields.io/badge/license-MIT-blue.svg)
[![Linux](https://github.com/grodansparadis/vscpl2drv-rs485/actions/workflows/build-linux.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl2drv-rs485/actions/workflows/build-linux.yml?query=branch%3Amaster+is%3Asuccess)
[![macOS](https://github.com/grodansparadis/vscpl2drv-rs485/actions/workflows/build-macos.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl2drv-rs485/actions/workflows/build-macos.yml?query=branch%3Amaster+is%3Asuccess)
[![Windows](https://github.com/grodansparadis/vscpl2drv-rs485/actions/workflows/build-windows.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl2drv-rs485/actions/workflows/build-windows.yml?query=branch%3Amaster+is%3Asuccess)
[![Project Status: Active](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)

<img src="https://vscp.org/images/logo.png" width="100">

  * **Available for**: Linux (Including Raspberry Pi 32/64-bit), Windows, MacOS
  * **Driver Linux**: libvscpl2drv-rs485.so
  * **Driver Windows**: vscpl2drv-rs485.dll
  * **Driver MacOS**: libvscpl2drv-rs485.dylib

## Introduction

A VSCP Level II driver for the [VSCP daemon](https://github.com/grodansparadis/vscpd)
that is the **master** of an RS-485/RS-422 segment with VSCP Level I nodes:
[VSCP Level I over RS-485/RS-422](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422).

* Polls the nodes for events, finds new nodes (one probe per polling round,
  as the specification suggests) and notices nodes that are gone.
* Events from the nodes reach the daemon with the driver's GUID and the
  node's address as the last byte.
* Events from the daemon go to every active node, or to the node a Level I
  protocol event names (register read/write, ...), also in Level II form
  (class 512-1023 with the destination GUID), so VSCP Works can scan and
  configure the nodes through the daemon.
* The ninth (address) bit with mark/space parity on ordinary serial ports and
  USB adapters, or plain 8N1; CRC-8/MAXIM-DOW (the specification's "DOW
  checksum"); direction control by the adapter, the Linux kernel RS-485 mode
  or RTS.
* The points the specification leaves open are configurable and documented
  in [the protocol description](./docs/protocol.md).

It is the twin of [vscpl1drv-rs485](https://github.com/grodansparadis/vscpl1drv-rs485)
(the Level I/CANAL driver) and uses the same protocol, bus master and serial
port code. It is built from [vscpl2drv-template](https://github.com/grodansparadis/vscpl2drv-template).

## Configuration

```json
{
    "device": "/dev/ttyUSB0",
    "baudrate": 115200,
    "addresses": "1-32",
    "poll-timeout": 20,
    "logging": { "file-enable-log": true, "file-path": "/var/log/vscp/vscpl2drv-rs485.log" }
}
```

All settings are in [docs/configure.md](./docs/configure.md); a complete
sample is *resources/linux/rs485.json*.

## Build

On Debian or Ubuntu, install the build tools and system libraries:

```bash
sudo apt update
sudo apt install build-essential cmake libssl-dev libexpat1-dev
```

Configure and build a Release version:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

CMake fetches the VSCP common code, spdlog and nlohmann/json automatically.
The driver library is written to `build/` (`libvscpl2drv-rs485.so` on Linux
or `.dylib` on macOS). To build the man page as well, install `pandoc` before
configuring.

Run the test on Linux or macOS with:

```bash
ctest --test-dir build --output-on-failure
```

For macOS prerequisites, Windows setup and build options, see
[docs/build.md](./docs/build.md).

## Documentation

* The [docs](./docs) folder (docsify): the RS-485 protocol, installing,
  building, configuring, testing and connecting hardware.
* `man vscpl2drv-rs485` after installing.
* The level II driver API is [described here](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_driver_interfaces).

## Other sources of information

 * The VSCP site - https://www.vscp.org
 * The VSCP document site - https://docs.vscp.org/
 * VSCP discussions - https://github.com/grodansparadis/vscp/discussions

## License

The driver code is released under the [MIT license](./LICENSE). The documentation is licensed under Creative Commons BY 4.0.
