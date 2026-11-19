% VSCPL2DRV-RS485(1) VSCP Level II driver
% Åke Hedman, Grodans Paradis AB
% November 2026

# NAME

vscpl2drv-rs485 - VSCP Level II driver for RS-485/RS-422, the bus master

# SYNOPSIS

**libvscpl2drv-rs485.so** (Linux), **vscpl2drv-rs485.dll** (Windows), **libvscpl2drv-rs485.dylib** (macOS)

A VSCP Level II driver. It is loaded by the VSCP daemon (**vscpd**(8)), VSCP Works or any other program that uses the VSCP Level II driver API, and is configured with the JSON configuration file described below. The path of the configuration file is given to the driver when it is opened (**path-config** in the daemon's driver configuration).

# DESCRIPTION

**vscpl2drv-rs485** is the master of a "VSCP Level I over RS-485/RS-422"
segment, loaded by the VSCP daemon. It polls the nodes on the segment for
VSCP Level I events, finds nodes that are added (one probe of an address
without a node per polling round) and notices nodes that disappear (after
*miss-limit* missed polls).

Events from the nodes reach the daemon as Level II events with the driver's
GUID and the node's RS-485 address as the last byte. Events from the daemon
are sent to every active node; Level I protocol events that name a node in
data byte 0 (register read and write, nickname, boot loader, ...) and Level I
events in Level II form (class 512-1023, destination GUID in data bytes
0-15) go to their node only. Level II only events are not sent.

Frames start with an address character with the ninth bit set (mark/space
parity), and end with a CRC-8/MAXIM-DOW.

# CONFIGURATION

The driver is configured with a JSON file (or a JSON string handed to the
driver as the configuration path). A sample configuration file is available
in the `resources/` folder of the repository and is installed to
*/usr/share/vscpl2drv-rs485/* by the Linux packages.

## Adding the driver to the VSCP daemon

Add the driver to the level II driver section of the VSCP daemon
configuration file (default */etc/vscp/vscpd.json*):

```json
"drivers" : {
  "level2" : [
    {
      "enable" : true,
      "name" : "rs485",
      "path-driver" : "/var/lib/vscp/drivers/level2/libvscpl2drv-rs485.so",
      "path-config" : "/etc/vscp/rs485.json",
      "guid" : "FF:FF:FF:FF:FF:FF:FF:FC:88:99:AA:BB:CC:DD:EE:FF"
    }
  ]
}
```

| Option | Description |
|--------|-------------|
| `enable` | Driver is loaded if `true`. |
| `name` | A name of your choice for the driver instance. |
| `path-driver` | Path to the driver shared library. |
| `path-config` | Path to the driver configuration file (described below). |
| `guid` | GUID for the driver instance. Events from the nodes get it with the node address as the last byte. |

## Driver configuration file

```json
{
  "device" : "/dev/ttyUSB0",
  "baudrate" : 115200,
  "addresses" : "1-127",
  "poll-timeout" : 20,
  "discovery-rounds" : 1,
  "miss-limit" : 3,
  "nine-bit" : true,
  "fixed-length" : false,
  "crc" : "dow",
  "broadcast" : false,
  "kernel-rs485" : false,
  "rts-direction" : false,
  "debug" : false,
  "write" : false,
  "rx-filter" : "0,0,0,-,0,0,0,-",
  "max-queue-size" : 32000,
  "logging" : {
    "console-enable-log" : false,
    "console-log-level" : "info",
    "console-pattern" : "[vscpl2drv-rs485 %c] [%^%l%$] %v",
    "file-enable-log" : true,
    "file-log-level" : "info",
    "file-pattern" : "[vscpl2drv-rs485 %c] [%^%l%$] %v",
    "file-path" : "/var/log/vscp/vscpl2drv-rs485.log",
    "file-max-size" : 5242880,
    "file-max-files" : 7
  }
}
```

### RS-485

*device*
:   Serial port of the RS-485 adapter (/dev/ttyUSB0, /dev/serial/by-id/...,
    COM3). Required.

*baudrate*
:   Baud rate of the segment, default 115200.

*addresses*
:   The node addresses on the segment (1-254), numbers and ranges such as
    "1-32,40". Default "1-127".

*poll-timeout*
:   Milliseconds to wait for the answer to a poll, default 20.

*discovery-rounds*
:   Probe one address without a node after this many polling rounds,
    default 1. 0 polls every configured address in every round.

*miss-limit*
:   Missed polls in a row before a node counts as gone, default 3.

*nine-bit*
:   Address character with the ninth bit set, default true. false = plain 8N1.

*fixed-length*
:   EVENT frames always carry eight data bytes, default false.

*crc*
:   "dow" (CRC-8/MAXIM-DOW, default) or "0x07" (CRC-8 with polynomial 0x07).

*broadcast*
:   Send events for everybody once, to address 0xFF, default false.

*kernel-rs485*
:   Linux: use the kernel RS-485 mode (RTS set while sending), default false.

*rts-direction*
:   Set RTS while sending a frame, default false.

### debug

Set to `true` to get extra debug output from the driver. Default: `false`.

### write

Set to `true` to allow the configuration to be written back by the driver
(remote configuration). Default: `false`.

### rx-filter

Filter for incoming events on the form

```
priority,class,type,GUID,filter-priority,filter-class,filter-type,filter-GUID
```

written as `"filter,mask"` pairs: `"0,0,0,-,0,0,0,-"` accepts all events.
See the [VSCP specification](https://grodansparadis.github.io/vscp-doc-spec/#/)
for a description of VSCP filters.

### max-queue-size

Maximum number of events held in the send/receive queues before events are
dropped. Default: `32000`.

### Logging

| Option | Description | Default |
|--------|-------------|---------|
| `console-enable-log` | Enable logging to the console. | `true` |
| `console-log-level` | One of `off`, `critical`, `error`, `warn`, `info`, `debug`, `trace`. | `info` |
| `console-pattern` | [spdlog pattern](https://github.com/gabime/spdlog/wiki/3.-Custom-formatting) for console log lines. | see above |
| `file-enable-log` | Enable logging to a rotating log file. | `false` |
| `file-log-level` | One of `off`, `critical`, `error`, `warn`, `info`, `debug`, `trace`. | `info` |
| `file-pattern` | spdlog pattern for file log lines. | see above |
| `file-path` | Path to the log file. | `/tmp/vscpl2drv-rs485.log` |
| `file-max-size` | Maximum size of a log file before it is rotated (bytes). | `5242880` |
| `file-max-files` | Maximum number of rotated log files to keep. | `7` |

filename

# FILES

*/var/lib/vscp/drivers/level2/libvscpl2drv-rs485.so*
:   The driver (Linux).

*resources/linux/rs485.json*
:   Sample configuration in the source repository (installed with the driver).

# SEE ALSO

**vscpd**(8), **vscpl1drv-rs485**(1), **vscp-dump**(1), **vscp-send**(1)

VSCP Level I over RS-485/RS-422: <https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422>

The source code and the full documentation: <https://github.com/grodansparadis/vscpl2drv-rs485>

The VSCP specification: <https://grodansparadis.github.io/vscp-doc-spec>

# COPYRIGHT

Copyright 2000-2026 Åke Hedman, Grodans Paradis AB. See the LICENSE file of the driver.
