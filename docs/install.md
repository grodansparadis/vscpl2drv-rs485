# Install the driver

## Install the driver on Linux

You can install the driver using the debian package with

> sudo apt install ./vscpl2drv-rs485_x.y.z.deb

the driver will be installed to */var/lib/vscp/drivers/level2*.

If you want to use the driver with the [VSCP Daemon](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_daemon) you need to add it to the vscpd.json file (*/etc/vscp/vscpd.json*). See [this document](https://grodansparadis.github.io/vscp/#/configuring_the_vscp_daemon) on how to do this.

You also need to set up a configuration file for the driver itself. If you don't need to dynamically edit the content of this file a good and safe location for it is in the */etc/vscp/* folder alongside the VSCP daemon configuration file.

If you need to do dynamic configuration a good place to put the file is in the */var/lib/vscp/vscpd/* folder or maybe a subfolder here. Make sure the _vscp_ user can read/write the location.

A sample configuration file (*rs485.json*) is made available in */usr/share/vscpl2drv-rs485/* after installation. The `vscp` user must be allowed to use the serial port (group `dialout`).

## Install the driver on Windows

Download the Windows package from the project's
[GitHub Releases](https://github.com/grodansparadis/vscpl2drv-rs485/releases)
page. Depending on the release, packages may include an NSIS installer (`.exe`)
and a portable ZIP archive.

## Install the driver from source

See [Build the driver from source](./build.md).

[filename](./bottom-copyright.md ':include')
