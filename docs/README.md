# vscpl2drv-rs485

A VSCP Level II driver for the [VSCP daemon](https://github.com/grodansparadis/vscpd)
that is the **master** of an RS-485/RS-422 segment with VSCP Level I nodes, as
described in [VSCP Level I over RS-485/RS-422](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422).

* It polls the nodes on the segment for events, finds nodes that are added
  (one probe per polling round, so new nodes are found without slowing down
  the others) and notices nodes that disappear.
* Events from the nodes reach the daemon as Level II events, with the
  driver's GUID and the node's address as the last byte.
* Events from the daemon go to every active node, or to the node a register
  read/write or other Level I protocol event names, so VSCP Works can scan
  and configure the nodes through the daemon.
* The ninth (address) bit is made with mark/space parity on ordinary serial
  ports and USB adapters, or the bus can run plain 8N1.
* Direction control by the adapter, the Linux kernel RS-485 mode or RTS.

It is the twin of [vscpl1drv-rs485](https://github.com/grodansparadis/vscpl1drv-rs485),
the Level I (CANAL) driver: both use the same protocol, bus master and serial
port code.

* [The RS-485 protocol](./protocol.md) and how the driver fills the gaps of
  the specification.
* [Configure the driver](./configure.md).
* [Install](./install.md) or [build](./build.md) it, and [test and use](./using.md) it.

[filename](./bottom-copyright.md ':include')
