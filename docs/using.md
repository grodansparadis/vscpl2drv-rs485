# Test and use the driver

## The test

`ctest --test-dir build` runs `tests/test-driver.cpp` (Linux and macOS). It
loads the built driver the way the VSCP daemon does and runs a real segment:
simulated nodes at the other end of a pseudo terminal answer the driver's
polls (8N1: a pseudo terminal has no parity bit). It checks that events from
the nodes arrive with the right GUID, head, class (also above 255), type and
data, that Level I events from the daemon reach every node, that a register
read in Level II form reaches only its node, that events that can not go on
a Level I bus are dropped, and that invalid configurations are refused.

The protocol and the bus master are tested in detail by the tests of
[vscpl1drv-rs485](https://github.com/grodansparadis/vscpl1drv-rs485), which
uses the same code.

## With the VSCP daemon

1. Install the driver (see [Install the driver](./install.md)).
2. Copy the sample configuration to */etc/vscp/rs485.json* and set `device`
   and `addresses` (see [Configure the driver](./configure.md)).
3. Add the driver to */etc/vscp/vscpd.json* and restart the daemon:

```bash
sudo systemctl restart vscpd
```

The `vscp` user the daemon runs as must be allowed to use the serial port
(group `dialout` on Debian and Raspberry Pi OS: `sudo usermod -aG dialout vscp`).

4. The driver log (*/var/log/vscp/vscpl2drv-rs485.log*) tells which nodes
   were found. With `"debug": true` and log level `debug` it also logs the
   events sent to the nodes and, once a minute, the bus statistics.
5. Connect [VSCP Works](https://grodansparadis.github.io/vscp-works-qt/#/) to
   the daemon: the events of the nodes show up in a session window, and node
   scan and configuration work on the nodes through the driver.

## On real hardware

* Connect A to A and B to B on every node, and the signal ground. Terminate
  both ends of the cable (120 Ω) and make sure the bus has fail-safe biasing.
* Every node needs its own address and the same baud rate.
* Start with a short address list that includes the nodes.
* Many CRC or frame errors in the statistics mean a wrong baud rate, A/B
  swapped, missing termination, two nodes with the same address, or nodes
  that use another CRC or frame length (`crc`, `fixed-length`). Nodes that
  never answer may need a longer `poll-timeout`, or `"nine-bit": false`.

[filename](./bottom-copyright.md ':include')
