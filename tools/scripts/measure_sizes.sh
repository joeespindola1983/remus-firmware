#!/usr/bin/env sh
set -eu

pio run -d firmware/blade -e blade -t size
pio run -d firmware/computer -e computer -t size
