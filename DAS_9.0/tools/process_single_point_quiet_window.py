from __future__ import annotations

import math
import re
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


DATA_DIR = Path(r"E:\data_2026_05_15_23_55_42_freq10000Hz_ext4_diff16_startCh0_endCh5888_rows5884_cols5000_pitch6.40m_interval1.60m_startM0.00_endM9420.80")
OUT_DIR = Path(r"D:\DAS_9.0(1)\DAS_9.0\outputs\single_point_quiet_window_20260517")
HIGH_PASS_HZ = 1.0
WINDOW_SECONDS = 30
SCAN_BIN_SECONDS = 1
CHUNK_SAMPLES = 5_000_000
SCAN_CHUNK_SECONDS = 512
EXACT_CANDIDATES = 200


class BiquadHighpass:
    def __init__(self, cutoff_hz: float, sample_rate: float, q: float = 1.0 / math.sqrt(2.0)):
        w0 = 2.0 * math.pi * cutoff_hz / sample_rate
        cw = math.cos(w0)
        sw = math.sin(w0)
        alpha = sw / (2.0 * q)

        b0 = (1.0 + cw) / 2.0
        b1 = -(1.0 + cw)
        b2 = (1.0 + cw) / 2.0
        a0 = 1.0 + alpha
        a1 = -2.0 * cw
        a2 = 1.0 - alpha

        self.b0 = b0 / a0
        self.b1 = b1 / a0
        self.b2 = b2 / a0
        self.a1 = a1 / a0
        self.a2 = a2 / a0
        self.z1 = 0.0
        self.z2 = 0.0

    def process(self, x: np.ndarray) -> np.ndarray:
        y = np.empty_like(x, dtype=np.float64)
        b0, b1, b2, a1, a2 = self.b0, self.b1, self.b2, self.a1, self.a2
        z1, z2 = self.z1, self.z2
        for i, sample in enumerate(x):
            out = b0 * sample + z1
            z1 = b1 * sample - a1 * out + z2
            z2 = b2 * sample - a2 * out
            y[i] = out
        self.z1, self.z2 = z1, z2
        return y


def parse_meta(folder_name: str) -> dict[str, float | int]:
    meta: dict[str, float | int] = {}
    patterns = {
        "frequency": r"freq(\d+)Hz",
        "extract": r"ext(\d+)",
        "diff": r"diff(\d+)",
        "start_ch": r"startCh(\d+)",
        "end_ch": r"endCh(\d+)",
        "rows": r"rows(\d+)",
        "cols": r"cols(\d+)",
        "pitch_m": r"pitch([\d.]+)m",
        "interval_m": r"interval([\d.]+)m",
        "start_m": r"startM([\d.]+)",
        "end_m": r"endM([\d.]+)",
    }
    for key, pattern in patterns.items():
        m = re.search(pattern, folder_name)
        if not m:
            continue
        text = m.group(1)
        meta[key] = float(text) if "." in text else int(text)
    return meta


def highpass_chunk(chunk: np.ndarray, filters: list[BiquadHighpass]) -> np.ndarray:
    y = chunk.astype(np.float64, copy=False)
    for filt in filters:
        y = filt.process(y)
    return y


def scan_quiet_window(data: np.memmap, sample_rate: int) -> tuple[int, float, np.ndarray]:
    bin_samples = sample_rate * SCAN_BIN_SECONDS
    full_bins = data.size // bin_samples
    sums_sq = np.zeros(full_bins, dtype=np.float64)
    counts = np.zeros(full_bins, dtype=np.int64)

    filters = [
        BiquadHighpass(HIGH_PASS_HZ, sample_rate),
        BiquadHighpass(HIGH_PASS_HZ, sample_rate),
    ]

    processed = 0
    total = full_bins * bin_samples
    while processed < total:
        take = min(CHUNK_SAMPLES, total - processed)
        chunk = np.asarray(data[processed:processed + take], dtype=np.float32)
        filtered = highpass_chunk(chunk, filters)

        local_offset = 0
        while local_offset < take:
            global_sample = processed + local_offset
            bin_index = global_sample // bin_samples
            part_count = min(take - local_offset, (bin_index + 1) * bin_samples - global_sample)
            part = filtered[local_offset:local_offset + part_count]
            sums_sq[bin_index] += float(np.dot(part, part))
            counts[bin_index] += part.size
            local_offset += part_count

        processed += take
        print(f"scanned {processed / sample_rate:.1f}s / {total / sample_rate:.1f}s", flush=True)

    per_sec_power = sums_sq / np.maximum(counts, 1)
    win_bins = WINDOW_SECONDS // SCAN_BIN_SECONDS
    window_power = np.convolve(per_sec_power, np.ones(win_bins, dtype=np.float64), mode="valid") / win_bins
    best_bin = int(np.argmin(window_power))
    best_rms = float(math.sqrt(window_power[best_bin]))
    return best_bin * bin_samples, best_rms, per_sec_power


def filter_segment(data: np.memmap, start: int, length: int, sample_rate: int) -> np.ndarray:
    pad = min(sample_rate * 10, start)
    read_start = start - pad
    read_end = min(data.size, start + length + sample_rate * 10)
    raw = np.asarray(data[read_start:read_end], dtype=np.float32)
    filters = [
        BiquadHighpass(HIGH_PASS_HZ, sample_rate),
        BiquadHighpass(HIGH_PASS_HZ, sample_rate),
    ]
    filtered = highpass_chunk(raw, filters)
    return filtered[pad:pad + length]


def ideal_highpass_segment(data: np.memmap, start: int, length: int, sample_rate: int) -> np.ndarray:
    pad = min(sample_rate * 10, start)
    read_start = start - pad
    read_end = min(data.size, start + length + sample_rate * 10)
    raw = np.asarray(data[read_start:read_end], dtype=np.float64)
    raw = raw - float(np.mean(raw))
    spectrum = np.fft.rfft(raw)
    freqs = np.fft.rfftfreq(raw.size, d=1.0 / sample_rate)
    spectrum[freqs < HIGH_PASS_HZ] = 0.0
    filtered = np.fft.irfft(spectrum, n=raw.size)
    return filtered[pad:pad + length]


def scan_quiet_window_fast(data: np.memmap, sample_rate: int) -> tuple[int, float, np.ndarray]:
    full_seconds = data.size // sample_rate
    second_power = np.zeros(full_seconds, dtype=np.float64)

    processed_seconds = 0
    while processed_seconds < full_seconds:
        take_seconds = min(SCAN_CHUNK_SECONDS, full_seconds - processed_seconds)
        start = processed_seconds * sample_rate
        end = start + take_seconds * sample_rate
        block = np.asarray(data[start:end], dtype=np.float32).reshape(take_seconds, sample_rate)
        mean = block.mean(axis=1, dtype=np.float64)
        centered = block.astype(np.float64, copy=False) - mean[:, None]
        second_power[processed_seconds:processed_seconds + take_seconds] = np.mean(centered * centered, axis=1)
        processed_seconds += take_seconds
        if processed_seconds % (SCAN_CHUNK_SECONDS * 8) == 0 or processed_seconds == full_seconds:
            print(f"quick scanned {processed_seconds:.0f}s / {full_seconds:.0f}s", flush=True)

    win_seconds = WINDOW_SECONDS
    kernel = np.ones(win_seconds, dtype=np.float64)
    rolling_power = np.convolve(second_power, kernel, mode="valid") / win_seconds

    candidate_count = min(EXACT_CANDIDATES, rolling_power.size)
    candidate_idx = np.argpartition(rolling_power, candidate_count - 1)[:candidate_count]
    candidate_idx = candidate_idx[np.argsort(rolling_power[candidate_idx])]

    best_start = int(candidate_idx[0] * sample_rate)
    best_rms = float("inf")
    exact_scores: list[tuple[int, float]] = []
    length = sample_rate * WINDOW_SECONDS
    for idx in candidate_idx:
        start = int(idx) * sample_rate
        seg = ideal_highpass_segment(data, start, length, sample_rate)
        rms = float(np.sqrt(np.mean(seg * seg)))
        exact_scores.append((int(idx), rms))
        if rms < best_rms:
            best_rms = rms
            best_start = start

    exact_scores.sort(key=lambda item: item[1])
    print("best exact candidates:", exact_scores[:10], flush=True)
    return best_start, best_rms, second_power


def draw_plot(y: np.ndarray, sample_rate: int, title: str, out_path: Path) -> None:
    width, height = 1600, 850
    left, right, top, bottom = 120, 60, 70, 105
    plot_w = width - left - right
    plot_h = height - top - bottom

    img = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("arial.ttf", 22)
        small = ImageFont.truetype("arial.ttf", 19)
        tick_font = ImageFont.truetype("arial.ttf", 17)
    except OSError:
        font = ImageFont.load_default()
        small = ImageFont.load_default()
        tick_font = ImageFont.load_default()

    # Robust y range, symmetric around zero.
    q = float(np.percentile(np.abs(y), 99.8))
    ymax = max(q * 1.25, 1e-6)
    nice = 10 ** math.floor(math.log10(ymax))
    ymax = math.ceil(ymax / nice * 2.0) / 2.0 * nice
    ymin = -ymax

    # Downsample using min/max envelope per pixel column.
    n = y.size
    x_minutes = np.arange(n, dtype=np.float64) / sample_rate / 60.0
    px_count = plot_w
    edges = np.linspace(0, n, px_count + 1, dtype=np.int64)

    def x_to_px(x_min: float) -> float:
        return left + (x_min / (WINDOW_SECONDS / 60.0)) * plot_w

    def y_to_px(v: float) -> float:
        return top + (ymax - v) / (ymax - ymin) * plot_h

    # Grid and axes.
    draw.rectangle([left, top, left + plot_w, top + plot_h], outline=(40, 40, 40), width=1)
    for i in range(11):
        xm = (WINDOW_SECONDS / 60.0) * i / 10.0
        x = x_to_px(xm)
        draw.line([x, top, x, top + plot_h], fill=(225, 225, 225), width=1)
        label = f"{xm:.2f}".rstrip("0").rstrip(".")
        bbox = draw.textbbox((0, 0), label, font=tick_font)
        draw.text((x - (bbox[2] - bbox[0]) / 2, top + plot_h + 18), label, fill=(45, 45, 45), font=tick_font)

    y_ticks = np.linspace(ymin, ymax, 9)
    for v in y_ticks:
        yy = y_to_px(float(v))
        draw.line([left, yy, left + plot_w, yy], fill=(225, 225, 225), width=1)
        label = f"{v:.3g}"
        bbox = draw.textbbox((0, 0), label, font=tick_font)
        draw.text((left - 12 - (bbox[2] - bbox[0]), yy - 9), label, fill=(45, 45, 45), font=tick_font)

    # Waveform.
    for px in range(px_count):
        a, b = edges[px], edges[px + 1]
        if b <= a:
            continue
        seg = y[a:b]
        y1 = y_to_px(float(np.min(seg)))
        y2 = y_to_px(float(np.max(seg)))
        x = left + px
        draw.line([x, y1, x, y2], fill=(0, 0, 0), width=1)

    # Labels.
    bbox = draw.textbbox((0, 0), title, font=font)
    draw.text(((width - (bbox[2] - bbox[0])) / 2, 24), title, fill=(25, 25, 25), font=font)

    xlabel = "Elapsed time (min)"
    bbox = draw.textbbox((0, 0), xlabel, font=small)
    draw.text((left + (plot_w - (bbox[2] - bbox[0])) / 2, height - 55), xlabel, fill=(35, 35, 35), font=small)

    ylabel = "Relative strain / phase (rad)"
    label_img = Image.new("RGBA", (360, 32), (255, 255, 255, 0))
    label_draw = ImageDraw.Draw(label_img)
    label_draw.text((0, 0), ylabel, fill=(35, 35, 35), font=small)
    label_img = label_img.rotate(90, expand=True)
    img.paste(label_img, (30, top + (plot_h - label_img.height) // 2), label_img)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)


def main() -> None:
    meta = parse_meta(DATA_DIR.name)
    sample_rate = int(meta.get("frequency", 10000))
    cols = int(meta.get("cols", 5000))
    bin_files = sorted(DATA_DIR.glob("*.bin"))
    if len(bin_files) != 1:
        raise RuntimeError(f"Expected one bin file, found {len(bin_files)}")
    bin_path = bin_files[0]
    if bin_path.stat().st_size % 4 != 0:
        raise RuntimeError("bin size is not float32 aligned")

    data = np.memmap(bin_path, dtype="<f4", mode="r")
    chunks = data.size / max(cols, 1)
    total_seconds = data.size / sample_rate
    print(f"file={bin_path}")
    print(f"samples={data.size}, chunks={chunks:.3f}, duration={total_seconds:.3f}s")

    start_sample, best_rms, per_sec_power = scan_quiet_window_fast(data, sample_rate)
    segment = ideal_highpass_segment(data, start_sample, sample_rate * WINDOW_SECONDS, sample_rate)
    start_s = start_sample / sample_rate
    end_s = start_s + WINDOW_SECONDS
    out_png = OUT_DIR / "single_point_highpass1Hz_quietest_30s.png"
    title = (
        f"Single-point DAS, quietest 30 s, high-pass {HIGH_PASS_HZ:g} Hz "
        f"({start_s:.1f}-{end_s:.1f} s)"
    )
    draw_plot(segment, sample_rate, title, out_png)

    stats_path = OUT_DIR / "single_point_highpass1Hz_quietest_30s_stats.txt"
    stats_path.write_text(
        "\n".join(
            [
                f"data_dir={DATA_DIR}",
                f"bin_file={bin_path}",
                f"sample_rate_hz={sample_rate}",
                f"total_samples={data.size}",
                f"total_duration_s={total_seconds:.6f}",
                f"saved_chunks_by_cols={chunks:.6f}",
                f"high_pass_hz={HIGH_PASS_HZ}",
                f"window_seconds={WINDOW_SECONDS}",
                f"quiet_start_s={start_s:.6f}",
                f"quiet_end_s={end_s:.6f}",
                f"quiet_start_min={start_s / 60.0:.6f}",
                f"quiet_end_min={end_s / 60.0:.6f}",
                f"quiet_rms_rad={best_rms:.9g}",
                f"segment_mean_rad={float(np.mean(segment)):.9g}",
                f"segment_std_rad={float(np.std(segment)):.9g}",
                f"segment_peak_to_peak_rad={float(np.max(segment) - np.min(segment)):.9g}",
            ]
        ),
        encoding="utf-8",
    )
    print(f"output_png={out_png}")
    print(f"stats={stats_path}")
    print(f"quiet_window={start_s:.3f}-{end_s:.3f}s rms={best_rms:.9g}")


if __name__ == "__main__":
    main()
