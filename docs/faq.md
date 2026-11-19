# FAQ

## Which nodes can I use?

Nodes that implement VSCP Level I over RS-485/RS-422: an address set in the
node, answers to POLL with an EVENT or NO_EVENTS. See
[The RS-485 protocol](./protocol.md).

## Should I use this driver or vscpl1drv-rs485?

Use this one with the VSCP daemon (it is a Level II driver). Use
[vscpl1drv-rs485](https://github.com/grodansparadis/vscpl1drv-rs485) where a
CANAL (Level I) driver is wanted, for example directly in VSCP Works or
vscp-cli. They work the same on the bus.

## My adapter has no ninth bit.

Most adapters can do parity, which is what the driver uses. If your nodes do
not use the ninth bit either, set `"nine-bit": false`.

## Where do I find the VSCP level II driver API specification?

[Here](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_driver_interfaces).

## Which license is used?

The driver code is released under the MIT license. The documentation is
licensed under Creative Commons BY 4.0.

[filename](./bottom-copyright.md ':include')
