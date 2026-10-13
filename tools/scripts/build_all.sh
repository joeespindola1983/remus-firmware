#!/usr/bin/env sh
set -eu

pio run -d firmware/blade -e blade
pio run -d firmware/computer -e computer
