import csv
import json
import math
import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage, signal


FOLDER = Path(
    r"E:\BaiduNetdiskDownload\1\data_all_2026_05_14_20_07_04_freq10000Hz_ext1_diff16_startCh0_endCh750_rows750_cols5000_pitch6.40m_interval0.40m_startM0.00_endM300.00"
)
BIN_PATH = FOLDER / "data_all_2026_05_14_20_07_04_0.bin"
OUT_DIR = Path(r"D:\DAS_9.0(1)\DAS_9.0\outputs\das_20260514_mud")

ROWS = 750
COLS = 5000
FS = 10000.0
DX = 0.4
X0 = 25.0
X_MIN = 25.0
X_MAX = 125.0

# Three visible stone-impact events from preliminary energy/slant-stack picking.
EVENTS = [
    {"name": "stone_1", "tau25_s": 32.85},
    {"name": "stone_2", "tau25_s": 39.50},
    {"name": "stone_3", "tau25_s": 47.35},
]


def font(size=14):
    for candidate in [
        r"C:\Windows\Fonts\arial.ttf",
        r"C:\Windows\Fonts\msyh.ttc",
    ]:
        if os.path.exists(candidate):
            return ImageFont.truetype(candidate, size=size)
    return ImageFont.load_default()


FONT = font(14)
FONT_SMALL = font(11)


def bwr(values, vmin=None, vmax=None):
    arr = np.asarray(values, dtype=np.float32)
    if vmin is None or vmax is None:
        p = np.nanpercentile(np.abs(arr), 98)
        vmin, vmax = -p, p
    den = max(1e-12, vmax - vmin)
    t = np.clip((arr - vmin) / den, 0, 1)
    rgb = np.empty(arr.shape + (3,), dtype=np.uint8)
    lo = t <= 0.5
    hi = ~lo
    q = np.zeros_like(t)
    q[lo] = t[lo] / 0.5
    q[hi] = (t[hi] - 0.5) / 0.5
    rgb[lo, 0] = (55 + 200 * q[lo]).astype(np.uint8)
    rgb[lo, 1] = (80 + 175 * q[lo]).astype(np.uint8)
    rgb[lo, 2] = (205 + 50 * q[lo]).astype(np.uint8)
    rgb[hi, 0] = (255 - 35 * q[hi]).astype(np.uint8)
    rgb[hi, 1] = (255 - 210 * q[hi]).astype(np.uint8)
    rgb[hi, 2] = (255 - 220 * q[hi]).astype(np.uint8)
    return rgb


def viridis_like(values):
    arr = np.asarray(values, dtype=np.float32)
    lo, hi = np.nanpercentile(arr, [2, 99.5])
    t = np.clip((arr - lo) / max(1e-12, hi - lo), 0, 1)
    stops = np.array(
        [
            [68, 1, 84],
            [59, 82, 139],
            [33, 145, 140],
            [94, 201, 98],
            [253, 231, 37],
        ],
        dtype=np.float32,
    )
    pos = t * (len(stops) - 1)
    i = np.clip(pos.astype(int), 0, len(stops) - 2)
    q = pos - i
    rgb = stops[i] * (1 - q[..., None]) + stops[i + 1] * q[..., None]
    return rgb.astype(np.uint8)


def save_heatmap(path, matrix, title, x_label, y_label, cmap="bwr", width=1200, height=720):
    top, left, right, bottom = 54, 72, 34, 58
    plot_w, plot_h = width - left - right, height - top - bottom
    if cmap == "bwr":
        rgb = bwr(matrix)
    else:
        rgb = viridis_like(matrix)
    img = Image.fromarray(rgb)
    img = img.resize((plot_w, plot_h), Image.Resampling.BILINEAR)
    canvas = Image.new("RGB", (width, height), "white")
    canvas.paste(img, (left, top))
    draw = ImageDraw.Draw(canvas)
    draw.rectangle([left, top, left + plot_w, top + plot_h], outline=(45, 70, 96), width=1)
    draw.text((left, 18), title, fill=(20, 40, 65), font=FONT)
    draw.text((left + plot_w // 2 - 60, height - 32), x_label, fill=(20, 40, 65), font=FONT)
    draw.text((8, top + plot_h // 2 - 10), y_label, fill=(20, 40, 65), font=FONT)
    canvas.save(path)


def save_xy_plot(path, series, title, x_label, y_label, width=1100, height=650):
    left, top, right, bottom = 76, 56, 34, 62
    plot_w, plot_h = width - left - right, height - top - bottom
    canvas = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(canvas)
    draw.rectangle([left, top, left + plot_w, top + plot_h], outline=(45, 70, 96), width=1)
    xs_all = np.concatenate([np.asarray(s["x"]) for s in series])
    ys_all = np.concatenate([np.asarray(s["y"]) for s in series])
    xmin, xmax = float(np.nanmin(xs_all)), float(np.nanmax(xs_all))
    ymin, ymax = float(np.nanmin(ys_all)), float(np.nanmax(ys_all))
    if math.isclose(ymin, ymax):
        ymin -= 1
        ymax += 1
    padx, pady = (xmax - xmin) * 0.04, (ymax - ymin) * 0.08
    xmin -= padx
    xmax += padx
    ymin -= pady
    ymax += pady
    for frac in np.linspace(0, 1, 6):
        x = left + int(frac * plot_w)
        y = top + int(frac * plot_h)
        draw.line([x, top, x, top + plot_h], fill=(225, 232, 241))
        draw.line([left, y, left + plot_w, y], fill=(225, 232, 241))
    colors = [(32, 105, 230), (230, 100, 40), (30, 150, 110), (130, 70, 200)]
    for idx, s in enumerate(series):
        x = np.asarray(s["x"], dtype=float)
        y = np.asarray(s["y"], dtype=float)
        pts = []
        for xx, yy in zip(x, y):
            px = left + int((xx - xmin) / (xmax - xmin) * plot_w)
            py = top + plot_h - int((yy - ymin) / (ymax - ymin) * plot_h)
            pts.append((px, py))
        if len(pts) > 1:
            draw.line(pts, fill=colors[idx % len(colors)], width=3, joint="curve")
        draw.text((left + 16, top + 14 + idx * 20), s.get("label", ""), fill=colors[idx % len(colors)], font=FONT_SMALL)
    draw.text((left, 18), title, fill=(20, 40, 65), font=FONT)
    draw.text((left + plot_w // 2 - 60, height - 34), x_label, fill=(20, 40, 65), font=FONT)
    draw.text((10, top + plot_h // 2 - 10), y_label, fill=(20, 40, 65), font=FONT)
    draw.text((left, top + plot_h + 12), f"{xmin:.2f}", fill=(70, 85, 105), font=FONT_SMALL)
    draw.text((left + plot_w - 56, top + plot_h + 12), f"{xmax:.2f}", fill=(70, 85, 105), font=FONT_SMALL)
    draw.text((left - 62, top), f"{ymax:.1f}", fill=(70, 85, 105), font=FONT_SMALL)
    draw.text((left - 62, top + plot_h - 12), f"{ymin:.1f}", fill=(70, 85, 105), font=FONT_SMALL)
    canvas.save(path)


def extract_continuous(mm, start_s, duration_s, row_min=0, row_max=ROWS):
    start_sample = max(0, int(round(start_s * FS)))
    sample_count = int(round(duration_s * FS))
    end_sample = min(int(mm.shape[0] * COLS), start_sample + sample_count)
    f0 = start_sample // COLS
    f1 = (end_sample + COLS - 1) // COLS
    offset = start_sample - f0 * COLS
    block = np.asarray(mm[f0:f1, row_min:row_max, :], dtype=np.float32)
    data = block.transpose(1, 0, 2).reshape(row_max - row_min, -1)
    return data[:, offset : offset + (end_sample - start_sample)], start_sample / FS


def preprocess_event(mm, tau25_s, x_min=X_MIN, x_max=X_MAX, before=0.12, duration=1.72, fs_out=500.0):
    row_min = int(round(x_min / DX))
    row_max = int(round(x_max / DX)) + 1
    data, actual_start = extract_continuous(mm, tau25_s - before, duration, row_min, row_max)
    data -= data.mean(axis=1, keepdims=True)
    sos = signal.butter(4, [10, 100], btype="bandpass", fs=FS, output="sos")
    data = signal.sosfiltfilt(sos, data, axis=1)
    down = int(round(FS / fs_out))
    data = signal.resample_poly(data, up=1, down=down, axis=1)
    t = actual_start + np.arange(data.shape[1]) / fs_out
    x = np.arange(row_min, row_max) * DX
    return data.astype(np.float32), t, x, fs_out


def dispersion_stack(data, t, x, tau25_s, velocities, freqs):
    t_rel = t - tau25_s
    x_rel = x - X0
    win_t = signal.windows.tukey(data.shape[1], alpha=0.18)
    win_x = signal.windows.tukey(data.shape[0], alpha=0.18)
    d = data * win_x[:, None] * win_t[None, :]
    spec = np.fft.rfft(d, axis=1)
    fft_freq = np.fft.rfftfreq(data.shape[1], d=t[1] - t[0])
    power = np.zeros((len(freqs), len(velocities)), dtype=np.float32)
    for i, f in enumerate(freqs):
        kf = int(np.argmin(np.abs(fft_freq - f)))
        u = spec[:, kf]
        amp_norm = np.sqrt(np.sum(np.abs(u) ** 2)) + 1e-12
        for j, v in enumerate(velocities):
            phase = np.exp(1j * 2 * np.pi * f * x_rel / v)
            power[i, j] = abs(np.sum(u * phase)) / amp_norm / math.sqrt(len(x_rel))
    return power


def fk_spectrum(data, fs_out, dx):
    d = data - data.mean(axis=1, keepdims=True)
    d *= signal.windows.tukey(d.shape[0], 0.2)[:, None]
    d *= signal.windows.tukey(d.shape[1], 0.2)[None, :]
    fk = np.fft.fftshift(np.fft.fft(np.fft.rfft(d, axis=1), axis=0), axes=0)
    f = np.fft.rfftfreq(d.shape[1], d=1.0 / fs_out)
    k = np.fft.fftshift(np.fft.fftfreq(d.shape[0], d=dx))
    p = np.log10(np.abs(fk) ** 2 + 1e-12)
    return p, f, k


def write_csv(path, rows, header):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    frames = BIN_PATH.stat().st_size // (ROWS * COLS * 4)
    mm = np.memmap(BIN_PATH, dtype=np.float32, mode="r", shape=(frames, ROWS, COLS))

    velocities = np.arange(70.0, 151.0, 1.0)
    freqs = np.arange(10.0, 101.0, 1.0)
    stacks = []
    event_rows = []

    for event in EVENTS:
        data, t, x, fs_out = preprocess_event(mm, event["tau25_s"])
        # Save event space-time panel. Rows are time, columns are distance for visual consistency.
        img_matrix = data.T
        save_heatmap(
            OUT_DIR / f"{event['name']}_25_125m_10_100Hz.png",
            img_matrix,
            f"{event['name']} filtered phase, 10-100 Hz, 25-125 m",
            "distance along cable (m)",
            "time after event",
            cmap="bwr",
        )

        power = dispersion_stack(data, t, x, event["tau25_s"], velocities, freqs)
        stacks.append(power)
        save_heatmap(
            OUT_DIR / f"{event['name']}_fc_dispersion_stack.png",
            power,
            f"{event['name']} f-c dispersion stack",
            "phase velocity (m/s)",
            "frequency (Hz)",
            cmap="viridis",
        )

        fk, f, k = fk_spectrum(data, fs_out, DX)
        fmask = (f >= 0) & (f <= 150)
        kmask = (k >= 0) & (k <= 1.25)
        save_heatmap(
            OUT_DIR / f"{event['name']}_fk_spectrum.png",
            fk[np.ix_(kmask, fmask)],
            f"{event['name']} f-k spectrum",
            "frequency (Hz)",
            "wavenumber (cycles/m)",
            cmap="viridis",
        )

        best_idx = np.argmax(power, axis=1)
        c = velocities[best_idx]
        event_rows.extend([[event["name"], float(fq), float(cv), float(power[i, best_idx[i]])] for i, (fq, cv) in enumerate(zip(freqs, c))])

    stack = np.median(np.stack(stacks, axis=0), axis=0)
    best_idx = np.argmax(stack, axis=1)
    c_curve = velocities[best_idx]
    c_smooth = ndimage.median_filter(c_curve, size=7)
    save_heatmap(
        OUT_DIR / "combined_fc_dispersion_stack.png",
        stack,
        "combined f-c dispersion stack, three stone impacts",
        "phase velocity (m/s)",
        "frequency (Hz)",
        cmap="viridis",
    )
    save_xy_plot(
        OUT_DIR / "phase_velocity_curve.png",
        [{"x": freqs, "y": c_smooth, "label": "picked phase velocity"}],
        "Picked 10-100 Hz phase velocity",
        "frequency (Hz)",
        "c (m/s)",
    )

    phase_rows = [[float(f), float(c), float(cs)] for f, c, cs in zip(freqs, c_curve, c_smooth)]
    write_csv(OUT_DIR / "phase_velocity_curve.csv", phase_rows, ["frequency_hz", "picked_c_m_s", "smoothed_c_m_s"])
    write_csv(OUT_DIR / "event_phase_velocity_picks.csv", event_rows, ["event", "frequency_hz", "picked_c_m_s", "stack_power"])

    # Very preliminary Scholte/Rayleigh approximation:
    # c is slightly below Vs for soft saturated sediment; use Vs ~= c / 0.92.
    depth = c_smooth / freqs / 3.0
    vs = c_smooth / 0.92
    order = np.argsort(depth)
    depth_s = depth[order]
    vs_s = vs[order]
    vs_s = ndimage.median_filter(vs_s, size=7)
    write_csv(
        OUT_DIR / "initial_vs_profile.csv",
        [[float(z), float(v)] for z, v in zip(depth_s, vs_s)],
        ["sensitivity_depth_m_approx_lambda_over_3", "vs_m_s_approx_c_over_0p92"],
    )
    save_xy_plot(
        OUT_DIR / "initial_vs_profile.png",
        [{"x": vs_s, "y": depth_s, "label": "initial Vs(z)"}],
        "Initial soft-sediment Vs profile estimate",
        "Vs (m/s)",
        "depth proxy (m)",
    )

    # Along-cable local apparent velocity from f-c stack in moving 28 m windows.
    along_rows = []
    centers = np.arange(39.0, 113.0, 4.0)
    vscan = np.arange(70.0, 151.0, 1.0)
    fband = np.arange(18.0, 75.0, 2.0)
    for center in centers:
        x_min = max(25.0, center - 14.0)
        x_max = min(125.0, center + 14.0)
        local_scores = []
        for event in EVENTS:
            data, t, x, fs_out = preprocess_event(mm, event["tau25_s"], x_min=x_min, x_max=x_max, before=0.10, duration=1.60)
            p = dispersion_stack(data, t, x, event["tau25_s"], vscan, fband)
            local_scores.append(np.nanmean(p, axis=0))
        local = np.median(np.stack(local_scores), axis=0)
        imax = int(np.argmax(local))
        along_rows.append([float(center), float(vscan[imax]), float(local[imax]), float(x_min), float(x_max)])
    write_csv(OUT_DIR / "along_cable_apparent_velocity.csv", along_rows, ["center_m", "velocity_m_s", "coherence_score", "window_x_min_m", "window_x_max_m"])
    save_xy_plot(
        OUT_DIR / "along_cable_apparent_velocity.png",
        [{"x": [r[0] for r in along_rows], "y": [r[1] for r in along_rows], "label": "local apparent c"}],
        "Along-cable apparent phase velocity, 25-125 m",
        "distance along cable (m)",
        "c (m/s)",
    )

    summary = {
        "bin": str(BIN_PATH),
        "frames": int(frames),
        "duration_s": float(frames * COLS / FS),
        "distance_interval_m": DX,
        "analysis_distance_m": [X_MIN, X_MAX],
        "events": EVENTS,
        "phase_velocity_m_s_median": float(np.nanmedian(c_smooth)),
        "phase_velocity_m_s_p10_p90": [float(np.nanpercentile(c_smooth, 10)), float(np.nanpercentile(c_smooth, 90))],
        "vs_m_s_median_approx": float(np.nanmedian(vs_s)),
        "vs_m_s_p10_p90_approx": [float(np.nanpercentile(vs_s, 10)), float(np.nanpercentile(vs_s, 90))],
        "depth_proxy_m_range": [float(np.nanmin(depth_s)), float(np.nanmax(depth_s))],
        "important_caveat": "Velocity search is bounded by the time-distance slope of the coherent stone-impact arrivals. The f-c ridge is a preliminary interpreted ridge, not a final inversion.",
    }
    write_csv(
        OUT_DIR / "soft_sediment_segments.csv",
        [
            [
                25.0,
                125.0,
                float(np.nanmedian(c_smooth)),
                float(np.nanpercentile(c_smooth, 10)),
                float(np.nanpercentile(c_smooth, 90)),
                float(np.nanmedian(vs_s)),
                "very_soft_saturated_mud_or_silty_mud",
                "high_confidence_for_low_stiffness_medium_confidence_for_exact_material",
            ]
        ],
        [
            "start_m",
            "end_m",
            "median_c_m_s",
            "p10_c_m_s",
            "p90_c_m_s",
            "approx_median_vs_m_s",
            "interpretation",
            "confidence_note",
        ],
    )
    (OUT_DIR / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    (OUT_DIR / "summary.md").write_text(
        "\n".join(
            [
                "# Shallow marine DAS preliminary products",
                "",
                f"- Duration: {summary['duration_s']:.2f} s",
                f"- Analysis distance: {X_MIN:.1f}-{X_MAX:.1f} m",
                f"- Three stone impacts at tau25: {', '.join(f'{e['tau25_s']:.2f}s' for e in EVENTS)}",
                f"- Picked phase velocity median: {summary['phase_velocity_m_s_median']:.1f} m/s",
                f"- Picked phase velocity P10-P90: {summary['phase_velocity_m_s_p10_p90'][0]:.1f}-{summary['phase_velocity_m_s_p10_p90'][1]:.1f} m/s",
                f"- Approximate Vs median: {summary['vs_m_s_median_approx']:.1f} m/s",
                f"- Approximate Vs P10-P90: {summary['vs_m_s_p10_p90_approx'][0]:.1f}-{summary['vs_m_s_p10_p90_approx'][1]:.1f} m/s",
                f"- Depth proxy range (lambda/3): {summary['depth_proxy_m_range'][0]:.2f}-{summary['depth_proxy_m_range'][1]:.2f} m",
                "",
                "Products:",
                "- stone_*_25_125m_10_100Hz.png: three extracted impact windows",
                "- stone_*_fk_spectrum.png: f-k spectra for each impact",
                "- combined_fc_dispersion_stack.png and phase_velocity_curve.csv: interpreted 10-100 Hz velocity ridge",
                "- initial_vs_profile.csv: first-pass Vs(z) proxy",
                "- soft_sediment_segments.csv: soft-mud interpretation segment",
                "",
                "Interpretation: very low velocities are consistent with very soft saturated mud/silty mud. This is an initial phase-velocity-to-Vs proxy, not a final geotechnical inversion.",
                "Caveat: the f-c ridge is constrained by the observed time-distance slope of the impact arrivals; exact Vs requires a proper layered Scholte/Rayleigh inversion and field calibration.",
            ]
        ),
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"outputs: {OUT_DIR}")


if __name__ == "__main__":
    main()
