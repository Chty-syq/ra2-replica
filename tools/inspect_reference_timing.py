"""Read-only extraction of timing constants from the locally identified YR build.

Does not launch, modify, or redistribute the executable. This is static evidence,
not an original-game runtime measurement. Unknown builds fail closed.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_SHA256 = "07188e1caf521610f9485cd302a1bec894d2dcc05bdb69c5593e794ed88cd1c2"


def inspect(path):
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError("Unrecognized executable; addresses must be audited for this build first")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections, optional_size = struct.unpack_from("<H", data, pe+6)[0], struct.unpack_from("<H", data, pe+20)[0]
    image_base = struct.unpack_from("<I", data, pe+24+28)[0]
    section_table = pe+24+optional_size

    def read(address, size):
        rva = address-image_base
        for index in range(sections):
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<4I", data, section_table+index*40+8)
            if virtual_address <= rva and rva+size <= virtual_address+raw_size:
                offset = raw_offset+rva-virtual_address
                return data[offset:offset+size]
        raise ValueError(f"Unmapped file address {address:#x}")

    def expect(address, expected):
        if read(address, len(expected)) != expected:
            raise ValueError(f"Instruction signature mismatch at {address:#x}")

    expect(0x714678, bytes.fromhex("b8 1f 85 eb 51 c1 e1 08"))  # x256 then integer /100
    expect(0x714694, bytes.fromhex("ba ff 00 00 00"))            # speed cap
    expect(0x427F86, bytes.fromhex("b8 84 03 00 00 99 f7 f9"))  # Rate -> 900 / raw Rate
    expect(0x4C9EC6, bytes.fromhex("bf 36 00 00 00"))           # factory stage count
    expect(0x4C9EF6, bytes.fromhex("b8 f7 12 da 4b f7 ea c1 fa 04"))  # signed /54
    expect(0x5FB2FC, bytes.fromhex("c1 e0 03 99 41 f7 f9"))     # long normalized delay
    table = [list(struct.unpack("<8i", read(0x832CEC+rate*32,32))) for rate in range(1,5)]
    return {
        "reference_kind": "static inspection; not a vanilla RA2 1.006 runtime trace",
        "file_version": "1.11-CnCNet-patch-213aa6b",
        "sha256": digest,
        "unit_speed_import": {"address": "0x71464A", "formula": "min(255, floor(clamp(Speed, 0, 100) * 256 / 100))"},
        "production": {"type_base_address": "0x711EE0", "cost_times_buildspeed_factor": struct.unpack("<d",read(0x7F4E80,8))[0],
                       "factory_address": "0x4C9EA0", "stage_count": 54, "interval_bounds": [1,255]},
        "buildup": {"type_import_address": "0x45F2DD", "frames_per_game_minute": struct.unpack("<d",read(0x7E27F8,8))[0],
                    "note": "Original divides this budget by SHP buildup frame count; replica currently uses the nominal total."},
        "animation": {"rate_import_address": "0x427F7B", "delay_formula": "Rate > 0 ? floor(900 / Rate) : 0; absent/-1 leaves default 1",
                      "normalize_address": "0x5FB2E0", "small_delay_rows_1_to_4_by_internal_index_0_to_7": table,
                      "large_delay_formula": "floor(delay * 8 / (internal_speed_index + 1))"},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(inspect(args.executable), ensure_ascii=False, indent=2)+"\n"
    if args.output:
        args.output.write_text(result,encoding="utf-8")
    else:
        print(result,end="")


if __name__ == "__main__":
    main()
