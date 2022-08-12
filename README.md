# :warning: Experimental iptsd fork :warning:

This is a fork of the libqzed branch of iptsd, with the following changes:
- Added support for SP7+ (requires [ithc driver](https://github.com/quo/ithc-linux)).
- Added support for Surface Pen on SP7/SP7+.

These changes are still experimental. Please report any issues you encounter.

---

# Intel Precise Touch & Stylus

This is the userspace part of IPTS (Intel Precise Touch & Stylus) for Linux.

With IPTS the Intel Management Engine acts as an interface for a touch
controller, returning raw capacitive touch data. This data is processed
outside of the ME and then relayed into the HID / input subsystem of the OS.

This daemon relies on a kernel driver that can be found here:
https://github.com/linux-surface/intel-precise-touch

For SP7+/SP8 and other devices using Intel's Touch Host Controller, you will
instead need the ithc driver:
https://github.com/quo/ithc-linux

The driver will establish and manage the connection to the IPTS hardware. It
will also set up an API that can be used by userspace to read the touch data
from IPTS.

The daemon will connect to the IPTS UAPI and start reading data. It will
parse the data, and generate input events using uinput devices. The reason for
doing this in userspace is that parsing the data requires floating points,
which are not allowed in the kernel.

**NOTE:** The multitouch code has not been tested on all devices. It is
very likely that it will still need adjustments to run correctly on some
devices. If you have a device with IPTS and want to test it, feel free to
open an issue or join ##linux-surface on Freenode IRC and get in touch.

### Building
You need to install g++, meson, and ninja through your distribution's
package manager.

Use meson and ninja to build iptsd, and then run it with sudo.

``` bash
$ meson build --wrap-mode=forcefallback --buildtype=debugoptimized -Dmarch=native
$ ninja -C build
$ sudo build/src/daemon/iptsd
```

### Installing
***Note:** iptsd is included as a package in the linux-surface repository.
Unless you are doing development, or need the latest version from master, it is
recommended to use the version from the repository.*

If you want to permanently install the daemon, we provide a systemd service
configuration that you can use. If your distro does not use systemd, you will
have to write your own definition, but it shouldn't be too hard.

Patches with support for other service managers are welcome!

You need to run the steps from "Building" first.

```bash
$ sudo ninja -C build install
$ sudo systemctl daemon-reload
$ sudo systemctl disable iptsd # see below
$ sudo systemctl start iptsd
```

The `systemctl disable` command does not actually disable the service, instead
it removes an old dependency on `multi-user.target`, which is no longer
necessary since the service is now triggered by device initialization.

---

Workaround on "Failed to set up systemctl service"
https://github.com/quo/iptsd/issues/1#issuecomment-1198511909 ->

```sudo semanage fcontext -a -t systemd_unit_file_t -s system_u /usr/lib/systemd/system/iptsd.service

sudo restorecon -vF /usr/lib/systemd/system/iptsd.service

sudo semanage fcontext -a -t usr_t -s system_u /usr/local/bin/iptsd
sudo semanage fcontext -a -t usr_t -s system_u /usr/local/bin/iptsd-reset-sensor
sudo semanage fcontext -a -t usr_t -s system_u /usr/local/bin/ipts-dump

sudo restorecon -vF /usr/local/bin/iptsd
sudo restorecon -vF /usr/local/bin/iptsd-reset-sensor
sudo restorecon -vF /usr/local/bin/ipts-dump
```

Also on SP7 you have to enable the gen7mt option for the ipts driver:

```echo options ipts gen7mt | sudo tee /etc/modprobe.d/ipts-gen7mt.conf```

Then repeat the systemctl steps from ### Installing and reboot (or reload ipts).
