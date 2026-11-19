# Configure the driver

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
| `guid` | GUID for the driver instance. Events from the nodes get this GUID with the node's address in the last byte, so leave the last two bytes zero. |

## Driver configuration file

```json
{
    "debug": false,
    "write": false,
    "device": "/dev/ttyUSB0",
    "baudrate": 115200,
    "addresses": "1-127",
    "poll-timeout": 20,
    "discovery-rounds": 1,
    "miss-limit": 3,
    "nine-bit": true,
    "fixed-length": false,
    "crc": "dow",
    "broadcast": false,
    "kernel-rs485": false,
    "rts-direction": false,
    "rx-filter": "0,0,0,-,0,0,0,-",
    "max-queue-size": 1000,
    "logging": {
        "console-enable-log": false,
        "console-log-level": "info",
        "console-pattern": "[vscpl2drv-rs485 %c] [%^%l%$] %v",
        "file-enable-log": true,
        "file-log-level": "info",
        "file-pattern": "[vscpl2drv-rs485 %c] [%^%l%$] %v",
        "file-path": "/var/log/vscp/vscpl2drv-rs485.log",
        "file-max-size": 5242880,
        "file-max-files": 7
    }
}
```

The configuration can also be given directly as `path-config`, as a JSON
string starting with `{`.

## RS-485 settings

| Option | Default | Description |
| :----- | :------ | :---------- |
| `device` | — | Serial port of the RS-485 adapter: `/dev/ttyUSB0`, `/dev/serial/by-id/...` (stays the same after a reboot), `/dev/ttyAMA0`, `COM3`. Required. |
| `baudrate` | `115200` | Baud rate of the segment. 1200-230400 everywhere; 460800, 500000, 921600 and 1000000 where the system has them. |
| `addresses` | `"1-127"` | The node addresses on the segment, numbers and ranges: `"1-32,40,50-52"`. Only these are polled. |
| `poll-timeout` | `20` | Milliseconds to wait for the answer to a poll. |
| `discovery-rounds` | `1` | Probe one address without a node after this many polling rounds. `0` = no discovery: every configured address is polled in every round. |
| `miss-limit` | `3` | Missed polls in a row before a node counts as gone. |
| `nine-bit` | `true` | Address character with the ninth bit set. `false` = plain 8N1, for nodes without the ninth bit. |
| `fixed-length` | `false` | EVENT frames always carry eight data bytes (15 characters). |
| `crc` | `"dow"` | `"dow"` = CRC-8/MAXIM-DOW (the specification's "DOW checksum"), `"0x07"` = CRC-8 with polynomial 0x07 as in vscp `crc8.c`. |
| `broadcast` | `false` | Send events for everybody once, to address 0xFF, instead of to each node. |
| `kernel-rs485` | `false` | Linux: use the kernel RS-485 mode (RTS set while sending). |
| `rts-direction` | `false` | Set RTS while sending a frame (direction control). |

See [The RS-485 protocol](./protocol.md) for what `nine-bit`, `fixed-length`,
`crc` and `broadcast` change on the wire.

### The ninth bit and the adapter

PC serial ports have no ninth data bit; the driver makes it with the parity
bit: mark parity for the address character, space parity for the rest. On
Linux received characters with the ninth bit set are recognised; on macOS
and Windows the master finds the frames from the poll timing, which works
because only the polled node answers. The adapter must support parity: most
USB adapters (FTDI, CP210x, CH340) and the UARTs of the Raspberry Pi do.

### Direction control

Adapters that switch between sending and receiving by themselves (nearly all
USB RS-485 adapters) need nothing. Otherwise use `kernel-rs485` (Linux) or
`rts-direction`. Adapters that receive their own transmission work: the
master recognises its echoed poll.

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

Maximum number of events held in the send/receive queues (and the bus
master's transmit queue) before events are dropped. Default: `32000`; the
sample uses `1000`.

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

[filename](./bottom-copyright.md ':include')
