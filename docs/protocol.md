# The RS-485 protocol

The driver is the **master** (server) of *VSCP Level I over RS-485/RS-422*
in the [VSCP specification](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422).
This page describes the protocol as the driver implements it, and how it
fills the places where the specification is not exact.

## The bus

* One master with address 0 and up to 254 nodes with fixed addresses
  (1-254). Every node has its own address, set in the node.
* Half duplex RS-485 (two wires) or RS-422 (four wires). Only the master
  starts a transfer: nodes talk when they are polled.
* 115200 baud by default, any other rate the nodes support.
* Characters are 8 data bits with a **ninth (address) bit**: it is set in
  the first character of every frame (the destination address) and clear in
  the others. Nodes can let their UART ignore everything until a character
  with the ninth bit and their own address arrives.

## Frames

| Character | EVENT (operation 1) | POLL (2), NO_EVENTS (3), NOOP (0) |
| :-------: | :------------------ | :-------------------------------- |
| 0 | Destination address (ninth bit set) | Destination address (ninth bit set) |
| 1 | Originating address (master = 0) | Originating address |
| 2 | Operation = 1 | Operation |
| 3 | Flags: bits 0-3 number of data bytes (0-8), bit 4 bit 8 of the class, bits 5-7 zero | CRC |
| 4 | VSCP class, bits 0-7 | |
| 5 | VSCP type | |
| 6 ... | 0-8 data bytes | |
| last | CRC | |

An EVENT is 7 + N characters (N data bytes), the others 4 characters. The CRC
covers every character before it, for the address character its low eight
bits.

## A polling round

```text
master -> node 5:   05* 00 02 89           POLL
node 5 -> master:   00* 05 03 1D           NO_EVENTS
master -> node 7:   07* 00 02 ..           POLL
node 7 -> master:   00* 07 01 03 0A 06 89 02 1A ..   EVENT CLASS1.MEASUREMENT temperature
master -> node 12:  0C* 00 02 ..           POLL of an address without a node (discovery)
                    (no answer within the poll time-out)
master -> node 5:   05* 00 01 03 14 09 ...  EVENT from the VSCP daemon
                                    * = ninth bit set
```

1. At start the master polls every configured address once to find the
   nodes that are there.
2. Then it polls the nodes that answered, one after the other. A node
   answers within the poll time-out with an EVENT, or with NO_EVENTS. (The
   specification allows a node to stay silent when it has nothing to send;
   the master then waits for the whole time-out, so NO_EVENTS is better.)
3. After every polling round (configurable) it polls **one** configured
   address that has no node, so that new nodes are found without slowing
   down the polling of the others, as the specification suggests. A
   segment with 127 addresses is thereby searched in 127 rounds instead of
   in one long pause.
4. A node that misses a number of polls in a row (3 by default) counts as
   gone and is only probed for again.
5. Events from the VSCP daemon are sent between the polls: an event and a poll take
   turns, so neither has to wait for the other.

Events that a node sends to another node (destination not 0) reach that node
directly: it sees its address on the bus. The master gives every event to
the VSCP daemon.

### Events from the VSCP daemon

A VSCP event is for everybody, but an RS-485 frame has one destination. The
master sends an event from the VSCP daemon to **every active node**, one frame
each. The Level I protocol events that name their node in data byte 0 go to
that node only: SET_NICKNAME (6), DROP_NICKNAME (8), READ_REGISTER (9),
WRITE_REGISTER (11), ENTER_BOOT_LOADER (12), PAGE_READ (24), PAGE_WRITE
(25), INCREMENT_REGISTER (29), DECREMENT_REGISTER (30), WHO_IS_THERE (31,
unless 0xFF = all), GET_MATRIX_INFO (33), GET_EMBEDDED_MDF (35),
EXTENDED_PAGE_READ (37), EXTENDED_PAGE_WRITE (38) and GET_EVENT_INTEREST
(40). The originating address of these frames is the master's, 0.

With `"broadcast": true` events for everybody are sent once, to address 0xFF, for
nodes that accept that as a broadcast address (the specification does not
define one).

## Where the specification is not exact

The specification chapter is short, and its own
[V1 interoperability proposal](https://github.com/grodansparadis/vscp-doc-spec/blob/master/proposals/rs485-v1-interoperability.md)
lists what has to be made exact. The driver follows the proposal's
provisional interpretation, and makes the points where implementations may
differ configurable:

| Question | The driver | Configurable |
| :------- | :--------- | :----------- |
| What is the ninth bit? | An address *marker*: set in the first character of a frame, which holds the eight bit destination address. | `"nine-bit": false` (8N1) |
| Length of POLL/NO_EVENTS | Four characters: destination, origin, operation, CRC. | — |
| Length of EVENT | 7 + N characters. The specification's table draws all eight data bytes (15 characters). | `"fixed-length": true` |
| Which CRC is "DOW"? | CRC-8/MAXIM-DOW (Dallas/Maxim 1-Wire): polynomial x⁸+x⁵+x⁴+1 reflected, initial value 0, no final XOR. Check value of "123456789": 0xA1. | `"crc": "0x07"` (vscp `crc8.c`, check 0xF4) |
| Addresses | 1-254 for nodes (the specification mentions 127; the default address list is 1-127). | `"addresses"` |
| Broadcast address | None. | `"broadcast": true` |
| Answer time | The specification says within one maximum frame time (about 10 ms at 115200). The master waits 20 ms by default. | `"poll-timeout"` |
| Bytes in a frame | At most three character times apart (at least 2 ms). | — |
| Priority | Not carried on the bus. Received events get priority 3 (normal). | — |

These are the points to check against existing nodes. The test program of
[vscpl1drv-rs485](https://github.com/grodansparadis/vscpl1drv-rs485) has golden
vectors for all frame types; both drivers use the same protocol code.

## Receiving

The master reads the answer character by character:

* A character with the ninth bit set always starts a new frame (a partial
  frame before it is counted as a frame error).
* When the port can not tell the ninth bit (Windows, macOS, 8N1) the first
  character after the poll starts the frame; only the polled node answers.
* The operation (and for EVENT the flags) give the length. Unknown
  operations, reserved flag bits, more than eight data bytes and wrong CRCs
  are errors. Nothing from a damaged frame reaches the VSCP daemon.
* An answer from another address than the polled one is not accepted (this
  shows two nodes with the same address).
* Adapters that echo what they send are handled: the master recognises its
  own POLL and keeps reading.

The counts are written to the driver log once a minute (log level `debug`).

## Level I events in Level II

A received event becomes a Level II event with the driver's GUID (from the
`guid` of the driver in *vscpd.json*) and the node's RS-485 address as its
last byte, the usual GUID of a Level I node behind an interface. The head has
priority normal and the hard coded nickname bit set.

Events from the VSCP daemon:

| Class | Sent |
| :---- | :--- |
| 0-511 (Level I), at most 8 data bytes | To every active node, or to the node a protocol event names |
| 512-1023 (Level I events in Level II form) | Data bytes 0-15 are the destination GUID. If its first 14 bytes are the driver's GUID, the event (class - 512, data after the GUID) is sent to the node in byte 15 |
| 1024 and up, or more than 8 data bytes | Not sent (Level II only) |

[filename](./bottom-copyright.md ':include')
