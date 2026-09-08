clear;

filePath = "C:\Users\admin\xwechat_files\wxid_wxf3v7jymejh22_11d2\msg\file\2026-09\data_all_2026_08_30_11_48_02_freq10000Hz_rows50_cols5000_pitch0.4_end40.0_start0.0_interval0.8\data_all_2026_08_30_11_48_02_0.bin";
[data, meta] = read_data_all_bin(filePath, "row-major");

frameIndex = 1;
fig = figure("Visible", "off", "Color", "white", "Position", [100, 100, 1200, 650]);
imagesc(meta.timeSeconds, meta.spatialPosition, data(:, :, frameIndex));
axis xy;
xlabel("Time (s)");
ylabel("Position");
title(sprintf("DAS binary preview - frame %d of %d", frameIndex, meta.frames));
colorbar;
colormap(turbo);

outputPath = fullfile(fileparts(mfilename("fullpath")), "data_all_first_frame.png");
exportgraphics(fig, outputPath, "Resolution", 160);
close(fig);
fprintf("Preview saved to %s\n", outputPath);
