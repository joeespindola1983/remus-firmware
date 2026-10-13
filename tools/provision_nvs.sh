#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 <serial-port> <device-id> <network-id> <64-hex-network-secret>" >&2
  exit 2
fi

port=$1
device_id=$2
network_id=$3
network_secret=$4

[[ $device_id =~ ^[1-9][0-9]*$ ]] || { echo "device-id must be a non-zero decimal integer" >&2; exit 2; }
[[ $network_id =~ ^[1-9][0-9]*$ ]] || { echo "network-id must be a non-zero decimal integer" >&2; exit 2; }
[[ $network_secret =~ ^[0-9a-fA-F]{64}$ ]] || { echo "network secret must contain exactly 64 hex characters" >&2; exit 2; }

framework_dir="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-espidf"
generator="$framework_dir/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
esptool="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/tool-esptoolpy/esptool.py"
[[ -f $generator && -f $esptool ]] || { echo "ESP-IDF PlatformIO packages are not installed" >&2; exit 1; }

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
chmod 700 "$work_dir"
csv="$work_dir/remus-nvs.csv"
image="$work_dir/remus-nvs.bin"
umask 077
printf '%s\n' \
  'key,type,encoding,value' \
  'remus,namespace,,' \
  "device_id,data,u64,$device_id" \
  "network_id,data,u32,$network_id" \
  "network_secret,data,hex2bin,$network_secret" > "$csv"

python3 "$generator" generate "$csv" "$image" 0x6000
python3 "$esptool" --chip esp32c3 --port "$port" write_flash 0x9000 "$image"
echo "Provisioned Remus NVS on $port (device and network identifiers only; secret not printed)."
