%% DAS 单通道波形 + 全通道 RMS 瀑布图（同一页面、滚轮查看）
clear; clc;

file = "E:\data_all_2026_09_02_00_02_19_freq10000Hz_ext8_diff8_startCh0_endCh3072_rows3072_cols5000_pitch3.20m_interval3.20m_startM3.20_endM9833.60\data_all_2026_09_02_00_02_19_0.bin";

fs = 10000; rows = 3072; cols = 5000; % 文件参数
dx = 3.2; startM = 3.2;               % 空间坐标参数
rowIndex = 50;                         % 上方显示的行，可改为1~3072
block = 100;                           % 每100点计算一次RMS（0.01秒）

% 检查无文件头 little-endian float32 数据
info = dir(file);
assert(~isempty(info), "找不到BIN文件：%s", file);
bytesPerFrame = rows*cols*4;
assert(mod(info.bytes, bytesPerFrame) == 0, ...
    "文件大小与rows=%d、cols=%d的float32帧不匹配", rows, cols);
frameCount = info.bytes/bytesPerFrame;
totalSamples = frameCount*cols;
assert(rowIndex >= 1 && rowIndex <= rows, "rowIndex必须在1~%d之间", rows);

% 连续拼接全部帧：[通道 x 时间]；分帧读取控制内存占用
allData = zeros(rows, totalSamples, "single");
fid = fopen(file, "rb", "ieee-le");
assert(fid >= 0, "无法打开BIN文件：%s", file);
fileGuard = onCleanup(@() fclose(fid));
for k = 1:frameCount
    frame = fread(fid, [cols, rows], "single=>single").'; % C/Python行优先
    assert(numel(frame) == rows*cols, "第%d帧读取不完整", k);
    idx = (k-1)*cols + (1:cols);
    allData(:, idx) = frame;
end
clear fileGuard frame;

% 全部通道执行1 Hz零相位高通，并计算短时RMS
% 每帧分别滤波，避免不同帧的相位基线跳变被误画成巨大信号
[z, p, gain] = butter(4, 1/(fs/2), "high");
[sos, gain] = zp2sos(z, p, gain);     % 二阶节形式在低截止频率时更稳定
blockCount = floor(totalSamples/block);
rmsMap = zeros(rows, blockCount, "single");
phase1Hz = [];
batchSize = 64;                        % 分批滤波，避免生成超大double矩阵
for firstRow = 1:batchSize:rows
    batchRows = firstRow:min(firstRow+batchSize-1, rows);
    filteredBatch = zeros(numel(batchRows), totalSamples, "single");
    for k = 1:frameCount
        idx = (k-1)*cols + (1:cols);
        segment = double(allData(batchRows, idx).');
        filteredBatch(:, idx) = single(filtfilt(sos, gain, segment).');
    end
    cube = reshape(single(filteredBatch(:, 1:blockCount*block)), ...
        numel(batchRows), block, blockCount);
    rmsMap(batchRows, :) = squeeze(sqrt(mean(cube.^2, 2)));
    if ismember(rowIndex, batchRows)
        phase1Hz = filteredBatch(batchRows == rowIndex, :).';
    end
end
clear allData filteredBatch cube;

% 时间、位置和稳健色标
t = (0:totalSamples-1)'/fs;
position = startM + (0:rows-1)*dx;
rmsTime = ((0:blockCount-1)*block + block/2)/fs;
colorMax = max(prctile(double(rmsMap(:)), 99.5), eps);
totalTime = totalSamples/fs;

% 同一页面：上方单通道波形，下方全部通道瀑布图
fig = figure("Color", "w", "Name", "DAS单通道波形与全通道瀑布图", ...
    "Position", [30, 40, 1750, 950]);
layout = tiledlayout(fig, 3, 1, "TileSpacing", "compact", "Padding", "compact");

axWave = nexttile(layout, 1);
plot(axWave, t, phase1Hz, "b", "LineWidth", 0.7); grid(axWave, "on");
xlabel(axWave, "时间 / s"); ylabel(axWave, "相位 / rad");
title(axWave, sprintf("第%d行，位置%.1f m（1 Hz高通）", ...
    rowIndex, position(rowIndex)));

axMap = nexttile(layout, [2 1]);
imagesc(axMap, position, rmsTime, rmsMap.'); axis(axMap, "xy");
xlabel(axMap, "位置 / m"); ylabel(axMap, "时间 / s");
title(axMap, sprintf("全部%d个通道的RMS瀑布图（1 Hz高通；滚轮查看时间）", rows));
colormap(axMap, jet(256)); clim(axMap, [0 colorMax]);
cb = colorbar(axMap, "southoutside");
cb.Label.String = "1 Hz高通后的RMS值（0为蓝色，高值为红色）";
set([axWave axMap], "FontSize", 10, "Box", "on");

% 当前2秒数据默认看1秒；长数据默认最多看5秒
viewSeconds = min(totalTime, min(5, max(1, totalTime/4)));
xlim(axWave, [0 viewSeconds]);
ylim(axMap, [0 viewSeconds]);
xlim(axMap, [position(1) position(end)]); % 瀑布图始终显示全部通道
fig.WindowScrollWheelFcn = ...
    @(~, event) scrollCombined(event, axWave, axMap, totalTime);

fprintf("读取成功：%d帧，%d通道，%.2f秒；滚轮可同步查看上下图\n", ...
    frameCount, rows, totalTime);

% 保留主要结果，便于继续分析
assignin("base", "rmsMap", rmsMap);
assignin("base", "phase1Hz", phase1Hz);

function scrollCombined(event, axWave, axMap, totalTime)
% 滚轮移动当前时间窗口的20%，并同步波形和瀑布图
currentLim = ylim(axMap);
window = diff(currentLim);
newStart = currentLim(1) + event.VerticalScrollCount*window*0.2;
newStart = min(max(newStart, 0), max(totalTime-window, 0));
newLim = [newStart newStart+window];
ylim(axMap, newLim);
xlim(axWave, newLim);
drawnow limitrate;
end
