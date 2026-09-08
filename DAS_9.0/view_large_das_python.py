import argparse
import os

import matplotlib.pyplot as plt
from matplotlib.widgets import Button, TextBox
import numpy as np


DEFAULT_FILE = (
    r"E:\data_all_2026_08_23_16_42_38_freq3333Hz_rows5550_cols5000_"
    r"pitch2.4_end26640.0_start0.0_interval4.8"
    r"\data_all_2026_07_23_16_42_38_101.bin"
)
SAMPLE_RATE = 3333.0
ROWS = 5550
COLS_PER_PACKET = 5000
CHANNEL_SPACING = 4.8
START_DISTANCE = 0.0
MAX_PREVIEW_TIMES = 1800
MAX_PREVIEW_CHANNELS = 1200


def open_packets(path):
    if not os.path.isfile(path):
        raise FileNotFoundError(f"File not found: {path}")

    file_bytes = os.path.getsize(path)
    if file_bytes % 4:
        raise ValueError("File size is not a multiple of four bytes.")

    values_per_packet = ROWS * COLS_PER_PACKET
    float_count = file_bytes // 4
    if float_count % values_per_packet:
        raise ValueError(
            "File length does not match rows and cols/packet: "
            f"float32 values={float_count:,}, remainder="
            f"{float_count % values_per_packet:,}"
        )

    packet_count = float_count // values_per_packet
    packets = np.memmap(
        path,
        dtype="<f4",
        mode="r",
        shape=(packet_count, ROWS, COLS_PER_PACKET),
        order="C",
    )
    return packets, packet_count


def sample_waterfall(packets, packet_count):
    total_samples = packet_count * COLS_PER_PACKET
    global_times = np.unique(
        np.linspace(
            0,
            total_samples - 1,
            min(MAX_PREVIEW_TIMES, total_samples),
            dtype=np.int64,
        )
    )
    channels = np.unique(
        np.linspace(
            0,
            ROWS - 1,
            min(MAX_PREVIEW_CHANNELS, ROWS),
            dtype=np.int64,
        )
    )

    packet_indices = global_times // COLS_PER_PACKET
    column_indices = global_times % COLS_PER_PACKET
    preview = np.empty((global_times.size, channels.size), dtype=np.float32)
    for packet_index in np.unique(packet_indices):
        time_mask = packet_indices == packet_index
        packet_columns = column_indices[time_mask]
        preview[time_mask, :] = np.asarray(
            packets[packet_index][np.ix_(channels, packet_columns)]
        ).T
    return preview, global_times, channels


def channel_series(packets, channel_number):
    if not 1 <= channel_number <= ROWS:
        raise ValueError(f"Channel must be in 1..{ROWS}.")
    return np.asarray(packets[:, channel_number - 1, :]).reshape(-1)


def show_viewer(path, packets, packet_count):
    total_samples = packet_count * COLS_PER_PACKET
    full_time = np.arange(total_samples, dtype=np.float64) / SAMPLE_RATE
    preview, preview_indices, preview_channels = sample_waterfall(
        packets, packet_count
    )
    preview_time = preview_indices / SAMPLE_RATE
    preview_distance = (
        START_DISTANCE + preview_channels * CHANNEL_SPACING
    )

    finite_abs = np.abs(preview[np.isfinite(preview)])
    color_limit = np.percentile(finite_abs, 99.0) if finite_abs.size else 1.0
    if not np.isfinite(color_limit) or color_limit <= 0:
        color_limit = 1.0

    channel1 = 1
    channel2 = 2
    series1 = channel_series(packets, channel1)
    series2 = channel_series(packets, channel2)

    fig = plt.figure(figsize=(15, 9))
    grid = fig.add_gridspec(
        3,
        1,
        height_ratios=[3.2, 1.15, 1.15],
        left=0.07,
        right=0.98,
        top=0.92,
        bottom=0.18,
        hspace=0.34,
    )
    ax_image = fig.add_subplot(grid[0, 0])
    ax_channel1 = fig.add_subplot(grid[1, 0])
    ax_channel2 = fig.add_subplot(grid[2, 0])

    image = ax_image.imshow(
        preview,
        aspect="auto",
        origin="lower",
        interpolation="nearest",
        extent=[
            preview_distance[0],
            preview_distance[-1],
            preview_time[0],
            preview_time[-1],
        ],
        cmap="seismic",
        vmin=-color_limit,
        vmax=color_limit,
    )
    ax_image.set_title(
        "DAS Waterfall (display-downsampled)"
        f" | fs={SAMPLE_RATE:g} Hz, rows={ROWS}, "
        f"cols/packet={COLS_PER_PACKET}, spacing={CHANNEL_SPACING:g} m"
    )
    ax_image.set_xlabel("Distance (m)")
    ax_image.set_ylabel("Time (s)")
    fig.colorbar(image, ax=ax_image, pad=0.01, label="Raw value")

    marker1 = ax_image.axvline(
        START_DISTANCE + (channel1 - 1) * CHANNEL_SPACING,
        linewidth=1.2,
        linestyle="--",
        color="black",
    )
    marker2 = ax_image.axvline(
        START_DISTANCE + (channel2 - 1) * CHANNEL_SPACING,
        linewidth=1.2,
        linestyle="--",
        color="green",
    )

    (line1,) = ax_channel1.plot(full_time, series1, linewidth=0.8)
    (line2,) = ax_channel2.plot(full_time, series2, linewidth=0.8)
    ax_channel1.set_title("Channel 1 | Distance 0 m")
    ax_channel2.set_title(f"Channel 2 | Distance {CHANNEL_SPACING:g} m")
    ax_channel1.set_ylabel("Raw value")
    ax_channel2.set_ylabel("Raw value")
    ax_channel2.set_xlabel("Time (s)")
    ax_channel1.grid(True, alpha=0.25)
    ax_channel2.grid(True, alpha=0.25)

    box1_axis = fig.add_axes([0.12, 0.07, 0.12, 0.045])
    box2_axis = fig.add_axes([0.34, 0.07, 0.12, 0.045])
    button_axis = fig.add_axes([0.51, 0.07, 0.12, 0.045])
    box1 = TextBox(box1_axis, "Channel 1: ", initial="1")
    box2 = TextBox(box2_axis, "Channel 2: ", initial="2")
    button = Button(button_axis, "Update")
    status = fig.text(
        0.68,
        0.084,
        f"Valid channel range: 1 ~ {ROWS}",
        fontsize=10,
        va="center",
    )

    def update(_event=None):
        try:
            new_channel1 = int(box1.text.strip())
            new_channel2 = int(box2.text.strip())
            new_series1 = channel_series(packets, new_channel1)
            new_series2 = channel_series(packets, new_channel2)
        except ValueError as exc:
            status.set_text(str(exc))
            fig.canvas.draw_idle()
            return

        line1.set_ydata(new_series1)
        line2.set_ydata(new_series2)
        distance1 = START_DISTANCE + (new_channel1 - 1) * CHANNEL_SPACING
        distance2 = START_DISTANCE + (new_channel2 - 1) * CHANNEL_SPACING
        ax_channel1.set_title(
            f"Channel {new_channel1} | Distance {distance1:g} m"
        )
        ax_channel2.set_title(
            f"Channel {new_channel2} | Distance {distance2:g} m"
        )
        ax_channel1.relim()
        ax_channel1.autoscale_view(scalex=False, scaley=True)
        ax_channel2.relim()
        ax_channel2.autoscale_view(scalex=False, scaley=True)
        marker1.set_xdata([distance1, distance1])
        marker2.set_xdata([distance2, distance2])
        status.set_text(f"Showing channels {new_channel1} and {new_channel2}")
        fig.canvas.draw_idle()

    button.on_clicked(update)
    box1.on_submit(update)
    box2.on_submit(update)

    print(f"File: {path}")
    print(f"Packets: {packet_count}")
    print(f"Logical data shape: ({ROWS}, {total_samples})")
    print(f"Duration: {total_samples / SAMPLE_RATE:.6f} s")
    print(f"Preview shape: {preview.shape}")
    plt.show()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path", nargs="?", default=DEFAULT_FILE)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    packets, packet_count = open_packets(args.path)
    total_samples = packet_count * COLS_PER_PACKET
    if args.check:
        preview, _, _ = sample_waterfall(packets, packet_count)
        print(f"File: {args.path}")
        print(f"Packets: {packet_count}")
        print(f"Logical data shape: ({ROWS}, {total_samples})")
        print(f"Duration: {total_samples / SAMPLE_RATE:.6f} s")
        print(f"Preview shape: {preview.shape}")
        print(f"Preview finite: {np.isfinite(preview).all()}")
        return

    show_viewer(args.path, packets, packet_count)


if __name__ == "__main__":
    main()
