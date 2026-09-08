function fig = das_bin_viewer_demo(filePath, selectedRow, visibleMode)
%DAS_BIN_VIEWER_DEMO Display a DAS BIN file in a two-panel viewer.
%   The upper panel shows the selected spatial row over continuous time.
%   The lower panel shows a time-space waterfall after removing the mean
%   of every spatial row within each 50x5000 frame. The source data is not
%   modified or overwritten.

if nargin < 1 || isempty(filePath)
    filePath = "C:\Users\admin\xwechat_files\wxid_wxf3v7jymejh22_11d2\msg\file\2026-09\data_all_2026_08_30_11_48_02_freq10000Hz_rows50_cols5000_pitch0.4_end40.0_start0.0_interval0.8\data_all_2026_08_30_11_48_02_0.bin";
end
if nargin < 2 || isempty(selectedRow)
    selectedRow = 30;
end
if nargin < 3 || isempty(visibleMode)
    visibleMode = "on";
end

% 自动加入读取函数所在目录，避免 MATLAB 当前文件夹不同导致报错。
readerFolder = fullfile(getenv('USERPROFILE'), 'Documents', 'MATLAB', 'DAS_bin_reader');
if exist('read_data_all_bin', 'file') ~= 2 && isfolder(readerFolder)
    addpath(readerFolder);
end
assert(exist('read_data_all_bin', 'file') == 2, '未找到 read_data_all_bin.m');

[data, meta] = read_data_all_bin(filePath, "row-major");
assert(selectedRow >= 1 && selectedRow <= meta.rows, ...
    "selectedRow must be between 1 and %d.", meta.rows);

% Concatenate the 15 frames into one continuous 50 x 75000 matrix.
continuousRaw = reshape(data, meta.rows, []);

% Remove only the per-frame, per-row DC offset for demonstration. This
% avoids artificial jumps between frames with different absolute offsets.
relativeFrames = data - mean(data, 2);
continuousRelative = reshape(relativeFrames, meta.rows, []);

sampleCount = size(continuousRelative, 2);
timeSeconds = (0:sampleCount-1) / meta.sampleRateHz;
selectedPosition = meta.spatialPosition(selectedRow);

% Average 50 samples (5 ms) per display row to keep the waterfall light.
displayBlock = 50;
displayRows = floor(sampleCount / displayBlock);
usableSamples = displayRows * displayBlock;
displayCube = reshape(continuousRelative(:, 1:usableSamples), ...
    meta.rows, displayBlock, displayRows);
waterfall = squeeze(mean(displayCube, 2)); % [space, display-time]
waterfallTime = ((0:displayRows-1) * displayBlock + (displayBlock-1)/2) ...
    / meta.sampleRateHz;

% The acquisition start time is encoded in this file name.
acquisitionStart = datetime(2026, 8, 30, 11, 48, 2);

fig = figure("Name", "DAS BIN 数据查看器", ...
    "NumberTitle", "off", ...
    "Visible", visibleMode, ...
    "Color", [0.965, 0.975, 0.99], ...
    "Position", [50, 40, 1700, 920]);

layout = tiledlayout(fig, 3, 1, ...
    "TileSpacing", "compact", "Padding", "compact");

fileInfo = dir(filePath);
sgtitle(layout, sprintf("DAS BIN 数据查看器  |  %s", fileInfo.name), ...
    "FontName", "Microsoft YaHei", "FontSize", 16, ...
    "FontWeight", "bold", "Interpreter", "none");

% Upper panel: selected-point waveform.
axWave = nexttile(layout, 1);
topWave = double(continuousRelative(selectedRow, :));
plot(axWave, timeSeconds, topWave, "Color", [0.08, 0.35, 0.95], ...
    "LineWidth", 1.0);
yline(axWave, 0, "Color", [0.35, 0.40, 0.48], "LineWidth", 0.8);
grid(axWave, "on");
axWave.GridColor = [0.72, 0.78, 0.86];
axWave.GridAlpha = 0.55;
xlim(axWave, [timeSeconds(1), timeSeconds(end)]);
xlabel(axWave, "时间 (s)", "FontName", "Microsoft YaHei");
ylabel(axWave, "相对值（原单位）", "FontName", "Microsoft YaHei");
title(axWave, sprintf("单点相对波形 — 行 %d，位置 %.3f m", ...
    selectedRow, selectedPosition), ...
    "FontName", "Microsoft YaHei", "FontWeight", "normal", ...
    "HorizontalAlignment", "left");

% Lower panel: time-space waterfall.
axWaterfall = nexttile(layout, [2, 1]);
imagesc(axWaterfall, meta.spatialPosition, waterfallTime, waterfall.');
axis(axWaterfall, "xy");
xlabel(axWaterfall, "空间位置 (m)", "FontName", "Microsoft YaHei");
ylabel(axWaterfall, "采集时间 (2026-08-30)", "FontName", "Microsoft YaHei");
title(axWaterfall, ...
    "DAS 相对值瀑布图（0为蓝色，负值越低越接近红色）", ...
    "FontName", "Microsoft YaHei", "FontWeight", "normal", ...
    "HorizontalAlignment", "left");

% 采用图二的蓝-青-绿-黄-红分层色谱，并把方向反转：
% 0及正值显示为蓝色，越接近-30越显示为红色。
clim(axWaterfall, [-30, 0]);
colormap(axWaterfall, flipud(jet(256)));
cb = colorbar(axWaterfall);
cb.Label.String = "相对值（单位待确认）";
cb.Label.FontName = "Microsoft YaHei";
cb.Ticks = -30:5:0;

tickSeconds = 0:1:floor(timeSeconds(end));
if tickSeconds(end) < timeSeconds(end)
    tickSeconds(end+1) = timeSeconds(end); %#ok<AGROW>
end
axWaterfall.YTick = tickSeconds;
axWaterfall.YTickLabel = string(acquisitionStart + seconds(tickSeconds), "HH:mm:ss.S");

set([axWave, axWaterfall], "FontName", "Microsoft YaHei", ...
    "FontSize", 11, "Box", "on", "Color", "white");

% Keep useful arrays in the base workspace for inspection in MATLAB.
assignin("base", "data", data);
assignin("base", "meta", meta);
assignin("base", "continuousRaw", continuousRaw);
assignin("base", "continuousRelative", continuousRelative);

outputPath = fullfile(fileparts(mfilename("fullpath")), ...
    "das_bin_viewer_demo.png");
exportgraphics(fig, outputPath, "Resolution", 150);
fprintf("Viewer preview saved to %s\n", outputPath);
end
