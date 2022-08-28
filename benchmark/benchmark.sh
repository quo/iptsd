#!/bin/bash -ve

cd ..

rm -rf build/

meson build --wrap-mode=forcefallback "$@"

ninja -C build src/daemon/iptsd

time build/src/daemon/iptsd benchmark/multitouch-1min.dat | tail -5

time build/src/daemon/iptsd benchmark/pendft-1min.dat | tail -5

