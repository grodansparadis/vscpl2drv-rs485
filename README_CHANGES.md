# Changes in vscpl2drv-rs485

Every change that goes into a release is listed here, newest release first.
Add each change under **Unreleased** when it is made. When a release is
tagged, rename **Unreleased** to the version and date, and start a new
**Unreleased** section above it.

## Unreleased

- First version: VSCP Level II driver that is the master of a "VSCP Level I over RS-485/RS-422" segment (VSCP specification), made from vscpl2drv-template (3a77fc0)
- Polling of the nodes, NO_EVENTS, start-up scan and discovery spread over the polling rounds, lost nodes after missed polls; events from the nodes become Level II events with the driver GUID and the node address as the last byte (priority normal, hard coded nickname)
- Events from the daemon: Level I classes to every active node or to the node a protocol event names; Level I events in Level II form (class 512-1023) to the node in their destination GUID when it is behind this driver; Level II only events are not sent
- Protocol, bus master and serial port shared with vscpl1drv-rs485: CRC-8/MAXIM-DOW (or 0x07), ninth bit with mark/space parity or 8N1, kernel RS-485 or RTS direction control, configurable frame length and broadcast address
- JSON configuration: device, baudrate, addresses, poll-timeout, discovery-rounds, miss-limit, nine-bit, fixed-length, crc, broadcast, kernel-rs485, rts-direction
- Test that loads the driver like the VSCP daemon and runs it against simulated nodes on a pseudo terminal
- Fixed from the template: after an instance was closed (also after a failed open) the next VSCPOpen in the same process crashed, because close() shut spdlog down; a second instance threw when it registered its logger; VSCPWrite leaked the event copy when the send queue was full
