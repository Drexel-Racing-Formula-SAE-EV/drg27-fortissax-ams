#!/usr/bin/env python3

import argparse
from pathlib import Path
import re
import sys


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    header_path = repo / "lib" / "ams_core" / "include" / "ams_core" / "ams_measurement.h"
    source_path = repo / "lib" / "ams_core" / "measurement" / "ams_measurement.c"
    cmake_path = repo / "lib" / "ams_core" / "CMakeLists.txt"
    config_path = build / "zephyr" / ".config"
    map_path = build / "zephyr" / "zephyr.map"

    for path in (header_path, source_path, cmake_path, config_path, map_path):
        require(path.is_file(), f"missing {path}")

    header = header_path.read_text(encoding="utf-8")
    source = source_path.read_text(encoding="utf-8")
    cmake = cmake_path.read_text(encoding="utf-8")
    config = config_path.read_text(encoding="utf-8", errors="replace")
    link_map = map_path.read_text(encoding="utf-8", errors="replace")

    require(
        re.search(
            r"^#define\s+AMS_MEASUREMENT_BUFFER_COUNT\s+2U$",
            header,
            flags=re.MULTILINE,
        ) is not None,
        "measurement store must use exactly two buffers",
    )

    required_header_tokens = (
        "buffer[AMS_MEASUREMENT_BUFFER_COUNT]",
        "reader_count[AMS_MEASUREMENT_BUFFER_COUNT]",
        "publication_drop_count",
        "next_sequence",
        "write_sequence",
        "published_index",
        "write_index",
        "write_in_progress",
        "ams_measurement_store_abort_write",
        "ams_measurement_store_copy_latest",
        "uncertainty_mA",
        "selected_range",
        "cell_age_ms",
        "temp_age_ms",
        "cell_avg8_mv",
        "cell_iir_mv",
    )

    for token in required_header_tokens:
        require(token in header, f"measurement contract missing: {token}")

    forbidden_source_tokens = (
        "malloc(",
        "calloc(",
        "realloc(",
        "free(",
    )

    for token in forbidden_source_tokens:
        require(token not in source, f"measurement core must remain heap-free: {token}")

    require(
        "sequence_increment(store->next_sequence)" in source,
        "writer attempt must allocate a nonzero sequence",
    )
    require(
        source.find("store->next_sequence = sequence;") <
        source.find("if(!store->write_in_progress"),
        "sequence allocation must precede writer availability decision",
    )
    require(
        "store->reader_count[index] == 0U" in source,
        "writer must reject a reader-pinned inactive buffer",
    )
    require(
        "store->reader_count[index]++" in source,
        "reader pin increment missing",
    )
    require(
        "store->reader_count[index]--" in source,
        "reader pin release missing",
    )

    pin_pos = source.find("store->reader_count[index]++")
    first_unlock_after_pin = source.find("store_unlock(store, key);", pin_pos)
    copy_pos = source.find(
        "memcpy(snapshot, &store->buffer[index], sizeof(*snapshot));",
        first_unlock_after_pin,
    )
    relock_pos = source.find("key = store_lock(store);", copy_pos)
    unpin_pos = source.find("store->reader_count[index]--", relock_pos)

    require(
        0 <= pin_pos < first_unlock_after_pin < copy_pos < relock_pos < unpin_pos,
        "snapshot copy must occur outside lock while reader pin is held",
    )

    require(
        "measurement/ams_measurement.c" in cmake,
        "measurement source is not part of ams_core",
    )

    require(
        "# CONFIG_AMS_BMS_AUTHORITY is not set" in config,
        "Z-007 must remain no-authority",
    )
    require(
        "# CONFIG_AMS_BALANCE_AUTHORITY is not set" in config,
        "Z-007 must remain no-balance-authority",
    )
    require(
        "CONFIG_HEAP_MEM_POOL_SIZE=0" in config,
        "Z-007 must remain application-heap-free",
    )

    # These helpers are referenced by the core startup contract and therefore
    # must survive final link garbage collection.
    for symbol in (
        "ams_measurement_snapshot_size_bytes",
        "ams_measurement_store_size_bytes",
        "ams_measurement_buffer_count",
    ):
        require(symbol in link_map, f"linked measurement contract symbol missing: {symbol}")

    print("PASS: coherent measurement-store contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
