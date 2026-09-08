% Preview the available data in this BIN file.
% Note: the file contains only 13x5000 float32 values, not 3072x5000.

file = "E:\data_2026_09_02_00_01_52_freq10000Hz_ext8_diff8_startCh0_endCh3072_rows3072_cols5000_pitch3.20m_interval3.20m_startM3.20_endM9833.60\data_2026_09_02_00_01_52_0.bin";
rows = 13;       % Actual row count calculated from the file size
cols = 5000;
fs = 10000;      % Hz

fid = fopen(file, "rb", "ieee-le");
assert(fid >= 0, "Cannot open the BIN file.");
data = fread(fid, [cols, rows], "single=>single").'; % C/Python row-major
fclose(fid);

t = (0:cols-1) / fs;
position = 3.2 + (0:rows-1) * 3.2; % Assumed from the folder name

figure("Color", "white", "Name", "BIN preview (13 available rows)");
imagesc(t, position, data);
axis xy;
xlabel("Time (s)");
ylabel("Position (m, assumed)");
title("Available BIN data: 13 x 5000 single");
colorbar;
colormap(turbo);

output = fullfile(fileparts(mfilename("fullpath")), "partial_bin_13rows.png");
exportgraphics(gcf, output, "Resolution", 150);
