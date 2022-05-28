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
You need to install git, a c compiler, meson, ninja through your
distributions package manager.

We are using libinih to parse configuration files. You should install it
if your distribution already packages it. All the major distros have it in
their repos already.

``` bash
$ sudo apt install libinih1 libinih-dev
$ sudo pacman -S libinih
$ sudo dnf install inih inih-devel
$ sudo zypper install libinih0 libinih-devel
```

If libinih is not found on your system, a copy will be downloaded and included
automatically.

Use meson and ninja to build iptsd, and then run it with sudo.

``` bash
$ git clone https://github.com/linux-surface/iptsd
$ cd iptsd
$ meson build
$ ninja -C build
$ sudo ./build/iptsd
```

You need to have the latest UAPI version of the IPTS kernel driver installed.
All recent linux-surface kernels already include this. To check if you have the
correct driver installed and loaded, check if a file called `/dev/ipts/0` exists.

``` bash
$ ls -l /dev/ipts/
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
$ sudo systemctl enable --now iptsd
```
