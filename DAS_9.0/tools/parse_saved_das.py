#!/usr/bin/env python3
"""
Parse DAS binary data saved by sava_data.cpp.

Supported folder name formats:
  data_all_<timestamp>_freq..._rows..._cols...
  data_<timestamp>_freq..._rows..._cols...

Save modes:
  - save all:   each frame is (rows, cols) float32
  - save single: each frame is (cols,) float32

The parser reads metadata from the folder name, sorts all .bin files by index,
and can:
  - print a summary
  - export data to .npy / .npz
  - export one frame to .csv
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Iterator, List, Sequence, Tuple

import numpy as np


FULL_PATTERN = re.compile(
    r"^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})"
    r"_freq(\d+)Hz_ext(\d+)_diff(\d+)_startCh(\d+)_endCh(\d+)"
    r"_rows(\d+)_cols(\d+)_pitch([-0-9.]+)m_interval([-0-9.]+)m"
    r"_startM([-0-9.]+)_endM([-0-9.]+)$"
)

COMPACT_PATTERN = re.compile(
    r"^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})"
    r"_freq(\d+)Hz_rows(\d+)_cols(\d+)"
    r"_pitch([-0-9.]+)_end([-0-9.]+)_start([-0-9.]+)_interval([-0-9.]+)$"
)

BIN_INDEX_PATTERN = re.compile(r"_(\d+)\.bin$", re.IGNORECASE)


@dataclass
class SaveMeta:
    folder_path: str
    folder_name: str
    save_mode: str
    is_save_all: bool
    timestamp: str
    frequency_hz: int
    extract_count: int
    diff_distance: int
    start_channel: int
    end_channel: int
    rows: int
    cols: int
    pitch_m: float
    interval_m: float
    start_m: float
    end_m: float
    inferred_extract: bool = False
    inferred_diff: bool = False
    inferred_channel_range: bool = False

    @property
    def frame_shape(self) -> Tuple[int, ...]:
        if self.is_save_all:
            return (self.rows, self.cols)
        return (self.cols,)

    @property
    def frame_float_count(self) -> int:
        count = 1
        for value in self.frame_shape:
            count *= value
        return count

    @property
    def frame_bytes(self) -> int:
        return self.frame_float_count * 4


def meter_per_raw_point_for_frequency(frequency_hz: int) -> float:
    if frequency_hz == 3333:
        return 1.2
    if frequency_hz == 2000:
        return 2.0
    return 0.4


def parse_folder_meta(folder: Path) -> SaveMeta:
    folder_name = folder.name

    match = FULL_PATTERN.match(folder_name)
    if match:
        prefix, timestamp, freq, ext, diff, start_ch, end_ch, rows, cols, pitch, interval, start_m, end_m = match.groups()
        return SaveMeta(
            folder_path=str(folder),
            folder_name=folder_name,
            save_mode="save_all" if prefix == "data_all_" else "save_single",
            is_save_all=(prefix == "data_all_"),
            timestamp=timestamp,
            frequency_hz=int(freq),
            extract_count=int(ext),
            diff_distance=int(diff),
            start_channel=int(start_ch),
            end_channel=int(end_ch),
            rows=int(rows),
            cols=int(cols),
            pitch_m=float(pitch),
            interval_m=float(interval),
            start_m=float(start_m),
            end_m=float(end_m),
        )

    match = COMPACT_PATTERN.match(folder_name)
    if match:
        prefix, timestamp, freq, rows, cols, pitch, end_m, start_m, interval = match.groups()
        frequency_hz = int(freq)
        interval_m = float(interval)
        pitch_m = float(pitch)
        meter_per_raw_point = meter_per_raw_point_for_frequency(frequency_hz)
        extract_count = max(1, int(round(interval_m / meter_per_raw_point))) if interval_m > 0 else 1
        diff_distance = max(1, int(round(pitch_m / meter_per_raw_point))) if pitch_m > 0 else 1
        start_channel = max(0, int(round(float(start_m) / interval_m))) if interval_m > 0 else 0
        end_channel = max(start_channel, int(round(float(end_m) / interval_m))) if interval_m > 0 else start_channel
        return SaveMeta(
            folder_path=str(folder),
            folder_name=folder_name,
            save_mode="save_all" if prefix == "data_all_" else "save_single",
            is_save_all=(prefix == "data_all_"),
            timestamp=timestamp,
            frequency_hz=frequency_hz,
            extract_count=extract_count,
            diff_distance=diff_distance,
            start_channel=start_channel,
            end_channel=end_channel,
            rows=int(rows),
            cols=int(cols),
            pitch_m=pitch_m,
            interval_m=interval_m,
            start_m=float(start_m),
            end_m=float(end_m),
            inferred_extract=True,
            inferred_diff=True,
            inferred_channel_range=True,
        )

    raise ValueError(
        "Cannot parse metadata from folder name. "
        "The folder must follow the sava_data naming convention.\n"
        f"Folder: {folder_name}"
    )


def sort_bin_files(folder: Path) -> List[Path]:
    bin_files = list(folder.glob("*.bin"))
    if not bin_files:
        raise FileNotFoundError(f"No .bin files found under: {folder}")

    def sort_key(path: Path) -> Tuple[int, str]:
        match = BIN_INDEX_PATTERN.search(path.name)
        index = int(match.group(1)) if match else 0
        return index, path.name.lower()

    return sorted(bin_files, key=sort_key)


def count_frames_in_file(bin_path: Path, meta: SaveMeta) -> int:
    file_size = bin_path.stat().st_size
    if file_size % 4 != 0:
        raise ValueError(f"File size is not aligned to float32: {bin_path}")
    if file_size % meta.frame_bytes != 0:
        raise ValueError(
            f"File size does not match frame size.\n"
            f"File: {bin_path}\n"
            f"Bytes: {file_size}\n"
            f"Frame bytes: {meta.frame_bytes}"
        )
    return file_size // meta.frame_bytes


def load_bin_file(bin_path: Path, meta: SaveMeta, dtype=np.float32) -> np.ndarray:
    raw = np.fromfile(bin_path, dtype=np.float32)
    frame_float_count = meta.frame_float_count
    if raw.size % frame_float_count != 0:
        raise ValueError(
            f"Float count does not match frame shape.\n"
            f"File: {bin_path}\n"
            f"Float count: {raw.size}\n"
            f"Frame float count: {frame_float_count}"
        )
    frame_count = raw.size // frame_float_count
    reshaped = raw.reshape((frame_count,) + meta.frame_shape)
    if dtype != np.float32:
        reshaped = reshaped.astype(dtype, copy=False)
    return reshaped


def iter_frames(folder: Path, dtype=np.float32) -> Iterator[np.ndarray]:
    meta = parse_folder_meta(folder)
    for bin_path in sort_bin_files(folder):
        yield load_bin_file(bin_path, meta, dtype=dtype)


def load_all_data(folder: Path, dtype=np.float32) -> Tuple[SaveMeta, np.ndarray]:
    meta = parse_folder_meta(folder)
    blocks = [load_bin_file(bin_path, meta, dtype=dtype) for bin_path in sort_bin_files(folder)]
    if not blocks:
        raise FileNotFoundError(f"No data blocks found under: {folder}")
    data = np.concatenate(blocks, axis=0)
    return meta, data


def load_frame(folder: Path, frame_index: int, dtype=np.float32) -> Tuple[SaveMeta, np.ndarray]:
    meta = parse_folder_meta(folder)
    if frame_index < 0:
        raise ValueError("frame_index must be >= 0")

    remaining = frame_index
    for bin_path in sort_bin_files(folder):
        frame_count = count_frames_in_file(bin_path, meta)
        if remaining < frame_count:
            block = load_bin_file(bin_path, meta, dtype=dtype)
            return meta, block[remaining]
        remaining -= frame_count

    raise IndexError(f"frame_index out of range: {frame_index}")


def summary_dict(folder: Path) -> dict:
    meta = parse_folder_meta(folder)
    files = sort_bin_files(folder)
    frames_per_file = [count_frames_in_file(path, meta) for path in files]
    total_frames = int(sum(frames_per_file))
    return {
        "meta": asdict(meta),
        "bin_files": [str(path) for path in files],
        "frames_per_file": frames_per_file,
        "total_frames": total_frames,
        "frame_shape": list(meta.frame_shape),
        "frame_bytes": meta.frame_bytes,
    }


def export_frame_csv(frame: np.ndarray, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if frame.ndim == 1:
        with output_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["sample_index", "value"])
            for index, value in enumerate(frame):
                writer.writerow([index, float(value)])
        return

    if frame.ndim == 2:
        with output_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            header = ["row_index"] + [f"col_{idx}" for idx in range(frame.shape[1])]
            writer.writerow(header)
            for row_index, row in enumerate(frame):
                writer.writerow([row_index] + [float(value) for value in row])
        return

    raise ValueError(f"Unsupported frame ndim for csv export: {frame.ndim}")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Parse DAS data saved by the upper computer. "
            "Both save-single and save-all folders are supported."
        )
    )
    parser.add_argument("folder", help="Saved data folder path.")
    parser.add_argument(
        "--summary",
        action="store_true",
        help="Print summary metadata as JSON.",
    )
    parser.add_argument(
        "--load-all",
        action="store_true",
        help="Load all frames and print final array shape.",
    )
    parser.add_argument(
        "--frame-index",
        type=int,
        default=None,
        help="Load one frame by zero-based global frame index.",
    )
    parser.add_argument(
        "--export-npy",
        default=None,
        help="Output .npy file path. Requires --load-all or --frame-index.",
    )
    parser.add_argument(
        "--export-npz",
        default=None,
        help="Output .npz file path with fields: data, meta_json. Requires --load-all or --frame-index.",
    )
    parser.add_argument(
        "--export-csv",
        default=None,
        help="Output .csv file path for one frame. Requires --frame-index.",
    )
    parser.add_argument(
        "--dtype",
        choices=["float32", "float64"],
        default="float32",
        help="Data type used after reading.",
    )
    return parser


def save_npz(path: Path, data: np.ndarray, meta: SaveMeta) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    np.savez(
        path,
        data=data,
        meta_json=json.dumps(asdict(meta), ensure_ascii=False, indent=2),
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    folder = Path(args.folder).expanduser().resolve()
    if not folder.is_dir():
        parser.error(f"Folder does not exist: {folder}")

    dtype = np.float64 if args.dtype == "float64" else np.float32

    try:
        meta = parse_folder_meta(folder)
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if args.summary:
        print(json.dumps(summary_dict(folder), ensure_ascii=False, indent=2))

    loaded_data = None

    if args.load_all:
        meta, loaded_data = load_all_data(folder, dtype=dtype)
        print(f"Loaded all data: shape={loaded_data.shape}, dtype={loaded_data.dtype}")

    if args.frame_index is not None:
        meta, frame = load_frame(folder, args.frame_index, dtype=dtype)
        loaded_data = frame
        print(f"Loaded frame {args.frame_index}: shape={frame.shape}, dtype={frame.dtype}")

    if args.export_npy:
        if loaded_data is None:
            parser.error("--export-npy requires --load-all or --frame-index")
        output = Path(args.export_npy).expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        np.save(output, loaded_data)
        print(f"Saved npy: {output}")

    if args.export_npz:
        if loaded_data is None:
            parser.error("--export-npz requires --load-all or --frame-index")
        output = Path(args.export_npz).expanduser().resolve()
        save_npz(output, loaded_data, meta)
        print(f"Saved npz: {output}")

    if args.export_csv:
        if args.frame_index is None:
            parser.error("--export-csv requires --frame-index")
        output = Path(args.export_csv).expanduser().resolve()
        export_frame_csv(loaded_data, output)
        print(f"Saved csv: {output}")

    if not args.summary and not args.load_all and args.frame_index is None:
        info = summary_dict(folder)
        print(json.dumps(info, ensure_ascii=False, indent=2))

    if meta.save_mode == "save_single":
        print(
            "Note: save-single folder metadata does not contain the saved row index. "
            "The parser can restore frames correctly, but the exact channel/row number "
            "must be known from the save-time configuration or UI record."
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
