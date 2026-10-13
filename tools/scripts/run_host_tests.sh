#!/usr/bin/env sh
set -eu

cmake -S tests/host -B .build/host
cmake --build .build/host
ctest --test-dir .build/host --output-on-failure
