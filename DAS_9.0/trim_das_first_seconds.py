import math
import os
import sys

import numpy as np


ROWS = 5550
SOURCE_COLS_PER_PACKET = 5000
SAMPLE_RATE = 3333
SECONDS = 5
CHANNEL_CHUNK = 50


def main(source_path, output_path):
    source_path = os.path.abspath(source_path)
    output_path = os.path.abspath(output_path)
    if not os.path.isfile(source_path):
        raise FileNotFoundError(source_path)
    if os.path.exists(output_path):
        raise FileExistsError(output_path)

    source_bytes = os.path.getsize(source_path)
    values_per_packet = ROWS * SOURCE_COLS_PER_PACKET
    if source_bytes % 4:
        raise ValueError("Source size is not a multiple of float32 bytes.")
    source_values = source_bytes // 4
    if source_values % values_per_packet:
        raise ValueError("Source does not contain complete DAS packets.")

    packet_count = source_values // values_per_packet
    output_samples = SAMPLE_RATE * SECONDS
    packets_needed = math.ceil(output_samples / SOURCE_COLS_PER_PACKET)
    if packets_needed > packet_count:
        raise ValueError("Source is shorter than the requested duration.")

    source = np.memmap(
        source_path,
        dtype="<f4",
        mode="r",
        shape=(packet_count, ROWS, SOURCE_COLS_PER_PACKET),
        order="C",
    )

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    partial_path = output_path + ".partial"
    if os.path.exists(partial_path):
        os.remove(partial_path)

    try:
        with open(partial_path, "wb") as output:
            for channel_start in range(0, ROWS, CHANNEL_CHUNK):
                channel_end = min(channel_start + CHANNEL_CHUNK, ROWS)
                channel_count = channel_end - channel_start
                block = np.empty(
                    (channel_count, output_samples), dtype="<f4"
                )

                destination_start = 0
                for packet_index in range(packets_needed):
                    remaining = output_samples - destination_start
                    take = min(SOURCE_COLS_PER_PACKET, remaining)
                    block[
                        :, destination_start : destination_start + take
                    ] = source[
                        packet_index,
                        channel_start:channel_end,
                        :take,
                    ]
                    destination_start += take

                block.tofile(output)

        expected_bytes = ROWS * output_samples * 4
        actual_bytes = os.path.getsize(partial_path)
        if actual_bytes != expected_bytes:
            raise ValueError(
                f"Output size mismatch: {actual_bytes} != {expected_bytes}"
            )
        os.replace(partial_path, output_path)
    finally:
        if os.path.exists(partial_path):
            os.remove(partial_path)

    print(f"Source: {source_path}")
    print(f"Output: {output_path}")
    print(f"Rows: {ROWS}")
    print(f"Samples per channel: {output_samples}")
    print(f"Duration: {output_samples / SAMPLE_RATE:.6f} s")
    print(f"Output bytes: {os.path.getsize(output_path)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: trim_das_first_seconds.py SOURCE OUTPUT")
    main(sys.argv[1], sys.argv[2])
