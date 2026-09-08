%% DAS 全通道 RMS 热力图（图二样式）
file = "E:\data_all_2026_09_02_00_02_19_freq10000Hz_ext8_diff8_startCh0_endCh3072_rows3072_cols5000_pitch3.20m_interval3.20m_startM3.20_endM9833.60\data_all_2026_09_02_00_02_19_0.bin";

fs = 10000; rows = 3072; cols = 5000; % 文件参数
dx = 3.2; startM = 3.2;               % 空间坐标参数
block = 100;                           % 每100点计算一次RMS（0.01秒）

% 检查文件并计算完整帧数
info = dir(file);
frameCount = info.bytes/(rows*cols*4); % single占4字节
assert(frameCount == floor(frameCount), "文件不是完整的float32帧");

% 分帧读取和计算，避免长期占用全部245 MB内存
blocksPerFrame = cols/block;
assert(blocksPerFrame == floor(blocksPerFrame), "cols必须能被block整除");
rmsMap = zeros(rows, frameCount*blocksPerFrame, "single");
fid = fopen(file, "rb", "ieee-le");
assert(fid >= 0, "无法打开BIN文件");
for k = 1:frameCount
    frame = fread(fid, [cols, rows], "single=>single").'; % 行优先转MATLAB矩阵
    frame = frame - mean(frame, 2);                       % 每通道去直流
    cube = reshape(frame, rows, block, blocksPerFrame);
    idx = (k-1)*blocksPerFrame + (1:blocksPerFrame);
    rmsMap(:, idx) = squeeze(sqrt(mean(cube.^2, 2)));
end
fclose(fid);

% 坐标及稳健色标：0为蓝色，高RMS为红色
position = startM + (0:rows-1)*dx;
time = ((0:size(rmsMap,2)-1)*block + block/2)/fs;
colorMax = prctile(double(rmsMap(:)), 99.5);
colorMax = max(colorMax, eps); % 防止全零数据导致色标范围无效

% 图二式单幅大热力图，水平色条置底
figure("Color", "w", "Name", "DAS RMS热力图", ...
    "Position", [40, 50, 1700, 850]);
imagesc(position, time, rmsMap.'); axis xy;
xlabel("位置 (m)"); ylabel("时间 (s)");
title(sprintf("DAS 全通道去直流 RMS（%d帧，%.2f秒）", ...
    frameCount, time(end)));
colormap(jet(256)); clim([0, colorMax]); % 图二色调：0蓝色，高值红色
cb = colorbar("southoutside");
cb.Label.String = "RMS值（0为蓝色，高值为红色）";
set(gca, "FontSize", 11, "Box", "on");

% 同时保留结果，便于在工作区继续分析
assignin("base", "rmsMap", rmsMap);
