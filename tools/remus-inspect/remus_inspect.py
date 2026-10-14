#!/usr/bin/env python3
"""Validate, summarize, and export append-only RMS1 evidence files."""

from __future__ import annotations

import argparse
import csv
import struct
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator

SESSION_HEADER_SIZE = 64
RECORD_HEADER_SIZE = 48
RECORD_CRC_SIZE = 4
MAX_RECORD_SIZE = 1024
IMU_BATCH = 0x03
GPS_FIX = 0x04
GAP = 0x05
SOURCE_MANIFEST = 0x0A


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


@dataclass(frozen=True)
class SessionHeader:
    recording_id: int
    computer_id: int
    pc_boot_id: int
    start_pc_mono_us: int
    firmware_hash: int
    profile_hash: int
    network_id: int


@dataclass(frozen=True)
class Record:
    offset: int
    record_type: int
    ordinal: int
    append_pc_mono_us: int
    origin_id: int
    origin_boot_id: int
    sequence: int
    payload: bytes


class RmsError(ValueError):
    pass


def read_header(stream: BinaryIO) -> SessionHeader:
    raw = stream.read(SESSION_HEADER_SIZE)
    if len(raw) != SESSION_HEADER_SIZE:
        raise RmsError("arquivo menor que o header RMS1 de 64 bytes")
    if raw[:4] != b"RMS1" or struct.unpack_from("<HH", raw, 4) != (1, 64):
        raise RmsError("magic, versao ou tamanho do header RMS1 invalido")
    expected_crc = struct.unpack_from("<I", raw, 60)[0]
    if crc32c(raw[:60]) != expected_crc:
        raise RmsError("CRC32C do header RMS1 invalido")
    fields = struct.unpack_from("<QQIQIIqII", raw, 8)
    return SessionHeader(fields[0], fields[1], fields[2], fields[3], fields[4],
                         fields[5], fields[7])


def records(stream: BinaryIO) -> Iterator[Record]:
    expected_ordinal = 1
    while True:
        offset = stream.tell()
        header = stream.read(RECORD_HEADER_SIZE)
        if not header:
            return
        if len(header) != RECORD_HEADER_SIZE:
            print(f"AVISO: cauda truncada no offset {offset}; prefixo anterior e valido",
                  file=sys.stderr)
            return
        if header[:4] != b"RMR1" or header[4] != 1 or struct.unpack_from("<H", header, 6)[0] != 48:
            raise RmsError(f"header de record invalido no offset {offset}")
        record_type = header[5]
        total_length = struct.unpack_from("<I", header, 8)[0]
        ordinal, append_us, origin_id = struct.unpack_from("<QQQ", header, 12)
        origin_boot_id, sequence, payload_length = struct.unpack_from("<III", header, 36)
        if total_length != RECORD_HEADER_SIZE + payload_length + RECORD_CRC_SIZE:
            raise RmsError(f"tamanhos inconsistentes no offset {offset}")
        if total_length > MAX_RECORD_SIZE:
            raise RmsError(f"record excede {MAX_RECORD_SIZE} bytes no offset {offset}")
        tail = stream.read(payload_length + RECORD_CRC_SIZE)
        if len(tail) != payload_length + RECORD_CRC_SIZE:
            print(f"AVISO: cauda truncada no offset {offset}; prefixo anterior e valido",
                  file=sys.stderr)
            return
        encoded = header + tail[:-4]
        if crc32c(encoded) != struct.unpack_from("<I", tail, payload_length)[0]:
            raise RmsError(f"CRC32C invalido no offset {offset}")
        if ordinal != expected_ordinal:
            raise RmsError(f"ordinal esperado {expected_ordinal}, encontrado {ordinal}")
        expected_ordinal += 1
        yield Record(offset, record_type, ordinal, append_us, origin_id,
                     origin_boot_id, sequence, tail[:-4])


def decode_imu(record: Record):
    if len(record.payload) < 36:
        raise RmsError(f"payload IMU curto no ordinal {record.ordinal}")
    origin_us, ingest_us, rssi = struct.unpack_from("<QQb", record.payload)
    batch = record.payload[20:]
    first_sample, batch_seq, sample_count, period_us = struct.unpack_from("<QIHH", batch)
    if batch_seq != record.sequence or not 1 <= sample_count <= 10:
        raise RmsError(f"cabecalho DATA_BATCH invalido no ordinal {record.ordinal}")
    if len(batch) != 16 + sample_count * 16:
        raise RmsError(f"tamanho DATA_BATCH invalido no ordinal {record.ordinal}")
    for index in range(sample_count):
        delta, ax, ay, az, gx, gy, gz, flags = struct.unpack_from(
            "<HhhhhhhH", batch, 16 + index * 16)
        yield (first_sample + index, origin_us + delta * 10, ingest_us, rssi,
               batch_seq, period_us, ax, ay, az, gx, gy, gz, flags)


def decode_manifest(record: Record):
    if len(record.payload) != 38:
        raise RmsError(f"tamanho SOURCE_MANIFEST invalido no ordinal {record.ordinal}")
    values = struct.unpack("<QIHBxHHHBxHBBHHHI", record.payload)
    if values[0] != record.origin_id or values[1] != record.origin_boot_id:
        raise RmsError(f"identidade SOURCE_MANIFEST invalida no ordinal {record.ordinal}")
    return {"imu_model": values[2], "accel_fs_g": values[3], "gyro_fs_dps": values[4],
            "dlpf_hz": values[5], "rate_hz": values[6], "lsb_per_g": values[12],
            "lsb_per_dps_x10": values[13]}


def decode_gap(record: Record):
    if len(record.payload) != 28:
        raise RmsError(f"tamanho GAP invalido no ordinal {record.ordinal}")
    from_batch, through_batch, first_sample, last_sample, reason, flags = \
        struct.unpack("<IIQQHH", record.payload)
    if from_batch > through_batch or flags & ~1:
        raise RmsError(f"campos GAP invalidos no ordinal {record.ordinal}")
    last_sample_known = not flags & 1
    if last_sample_known and last_sample < first_sample:
        raise RmsError(f"intervalo GAP invalido no ordinal {record.ordinal}")
    return {"from_batch": from_batch, "through_batch": through_batch,
            "first_sample": first_sample,
            "last_sample": last_sample if last_sample_known else None,
            "reason": reason}


def decode_gps(record: Record):
    if len(record.payload) != 40:
        raise RmsError(f"tamanho GPS_FIX invalido no ordinal {record.ordinal}")
    values = struct.unpack("<QiiIHBBHHqI", record.payload)
    fix = {"pc_mono_us": values[0], "latitude_e7": values[1],
           "longitude_e7": values[2], "speed_mm_per_sec": values[3],
           "course_centi_deg": values[4], "satellites": values[5],
           "fix_quality": values[6], "hdop_x100": values[7],
           "flags": values[8], "utc_ms": values[9], "age_ms": values[10]}
    if fix["pc_mono_us"] == 0 or fix["flags"] & ~0x7F:
        raise RmsError(f"campos GPS_FIX invalidos no ordinal {record.ordinal}")
    if fix["flags"] & 0x02 and not (
        -900000000 <= fix["latitude_e7"] <= 900000000 and
        -1800000000 <= fix["longitude_e7"] <= 1800000000
    ):
        raise RmsError(f"coordenadas GPS_FIX invalidas no ordinal {record.ordinal}")
    if fix["flags"] & 0x08 and fix["course_centi_deg"] >= 36000:
        raise RmsError(f"course GPS_FIX invalido no ordinal {record.ordinal}")
    return fix


def inspect(path: Path, csv_path: Path | None) -> None:
    source_stats = defaultdict(lambda: {"batches": 0, "samples": 0, "gaps": 0,
                                        "explicit_gaps": 0, "lost_batches": 0,
                                        "next_batch": None, "next_sample": None})
    record_count = 0
    gps_stats = {"records": 0, "position_fixes": 0, "ubx_coherent": 0,
                 "last": None}
    manifests = {}
    writer = None
    output = None
    try:
        if csv_path:
            output = csv_path.open("w", newline="", encoding="utf-8")
            writer = csv.writer(output)
            writer.writerow(("source_role", "origin_id", "origin_boot_id", "batch_seq",
                             "sample_index", "origin_mono_us", "ingest_pc_mono_us",
                             "rssi_dbm", "nominal_period_us", "ax_raw", "ay_raw", "az_raw",
                             "gx_raw", "gy_raw", "gz_raw", "ax_g", "ay_g", "az_g",
                             "gx_dps", "gy_dps", "gz_dps", "flags"))
        with path.open("rb") as stream:
            header = read_header(stream)
            for record in records(stream):
                record_count += 1
                if record.record_type == SOURCE_MANIFEST:
                    manifests[(record.origin_id, record.origin_boot_id)] = decode_manifest(record)
                    continue
                if record.record_type == GAP:
                    gap = decode_gap(record)
                    stats = source_stats[(record.origin_id, record.origin_boot_id)]
                    stats["explicit_gaps"] += 1
                    stats["lost_batches"] += gap["through_batch"] - gap["from_batch"] + 1
                    continue
                if record.record_type == GPS_FIX:
                    fix = decode_gps(record)
                    gps_stats["records"] += 1
                    gps_stats["position_fixes"] += bool(fix["flags"] & 0x02)
                    gps_stats["ubx_coherent"] += bool(fix["flags"] & 0x40)
                    gps_stats["last"] = fix
                    continue
                if record.record_type != IMU_BATCH:
                    continue
                samples = list(decode_imu(record))
                key = (record.origin_id, record.origin_boot_id)
                stats = source_stats[key]
                first_sample = samples[0][0]
                if stats["next_batch"] is not None and (
                    record.sequence != stats["next_batch"] or first_sample != stats["next_sample"]
                ):
                    stats["gaps"] += 1
                stats["next_batch"] = record.sequence + 1
                stats["next_sample"] = first_sample + len(samples)
                stats["batches"] += 1
                stats["samples"] += len(samples)
                if writer:
                    role = "computer" if record.origin_id == header.computer_id else "blade"
                    manifest = manifests.get(key)
                    for sample in samples:
                        axes = sample[6:12]
                        converted = ("",) * 6
                        if manifest and manifest["lsb_per_g"] and manifest["lsb_per_dps_x10"]:
                            converted = tuple(value / manifest["lsb_per_g"] for value in axes[:3]) + \
                                tuple(value * 10 / manifest["lsb_per_dps_x10"] for value in axes[3:])
                        writer.writerow((role, record.origin_id, record.origin_boot_id,
                                         sample[4], sample[0], sample[1], sample[2], sample[3],
                                         sample[5], *axes, *converted, sample[12]))
    finally:
        if output:
            output.close()

    print(f"RMS1 valido: {path}")
    print(f"recording_id={header.recording_id:016X} computer_id={header.computer_id} "
          f"pc_boot_id={header.pc_boot_id} records={record_count} sources={len(source_stats)}")
    for (origin_id, boot_id), stats in sorted(source_stats.items()):
        role = "computer" if origin_id == header.computer_id else "blade"
        print(f"source={role} id={origin_id} boot={boot_id} batches={stats['batches']} "
              f"samples={stats['samples']} inferred_gaps={stats['gaps']} "
              f"explicit_gaps={stats['explicit_gaps']} "
              f"lost_batches={stats['lost_batches']}")
        manifest = manifests.get((origin_id, boot_id))
        if manifest:
            print(f"  imu_model=0x{manifest['imu_model']:02X} accel=+/-{manifest['accel_fs_g']}g "
                  f"gyro=+/-{manifest['gyro_fs_dps']}dps dlpf_hz={manifest['dlpf_hz']} "
                  f"rate_hz={manifest['rate_hz']} lsb_per_g={manifest['lsb_per_g']} "
                  f"lsb_per_dps_x10={manifest['lsb_per_dps_x10']}")
    print(f"gps_records={gps_stats['records']} position_fixes={gps_stats['position_fixes']} "
          f"ubx_coherent={gps_stats['ubx_coherent']}")
    if gps_stats["last"] is not None:
        fix = gps_stats["last"]
        latitude = fix["latitude_e7"] / 10_000_000 if fix["flags"] & 0x02 else "unavailable"
        longitude = fix["longitude_e7"] / 10_000_000 if fix["flags"] & 0x02 else "unavailable"
        print(f"  last_gps latitude={latitude} longitude={longitude} "
              f"satellites={fix['satellites']} fix_quality={fix['fix_quality']} "
              f"hdop_x100={fix['hdop_x100']} age_ms={fix['age_ms']} "
              f"flags=0x{fix['flags']:04X}")
    if csv_path:
        print(f"CSV: {csv_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path, help="arquivo RM_*.rms")
    parser.add_argument("--csv", type=Path, help="exporta todas as amostras IMU brutas")
    args = parser.parse_args()
    try:
        inspect(args.file, args.csv)
    except (OSError, RmsError) as error:
        print(f"ERRO: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
