%% 快速查看一个 DAS 软件通道（只读目标通道，不加载全部 245 MB）
file = "E:\data_all_2026_09_02_00_02_19_freq10000Hz_ext8_diff8_startCh0_endCh3072_rows3072_cols5000_pitch3.20m_interval3.20m_startM3.20_endM9833.60\data_all_2026_09_02_00_02_19_0.bin";

fs = 10000;       % 采样率 Hz
rows = 3072;      % 每帧通道数
cols = 5000;      % 每帧每通道的采样点数
channelNo = 200;  % 要查看的软件通道号，可修改为 0~3071

% 根据文件大小计算完整帧数
info = dir(file);
frameCount = info.bytes/(rows*cols*4); % single 占4字节
assert(frameCount == floor(frameCount), "文件不是完整的float32帧");

% 每帧只读取目标通道，速度快、占用内存小
fid = fopen(file, "rb", "ieee-le");
assert(fid >= 0, "无法打开BIN文件");
phase = zeros(cols*frameCount, 1);
for k = 1:frameCount
    offset = ((k-1)*rows*cols + channelNo*cols)*4;
    fseek(fid, offset, "bof");
    phase((k-1)*cols + (1:cols)) = fread(fid, cols, "single=>double");
end
fclose(fid);

% 每帧分别做1 Hz高通，避免不同帧基线造成假跳变
[b, a] = butter(4, 1/(fs/2), "high");
phase1Hz = zeros(size(phase));
for k = 1:frameCount
    idx = (k-1)*cols + (1:cols);
    phase1Hz(idx) = filtfilt(b, a, phase(idx));
end
t = (0:numel(phase1Hz)-1)'/fs;

% 显示单点连续波形
figure("Color", "w", "Name", "DAS单点查看");
plot(t, phase1Hz, "b"); grid on;
xlabel("时间 / s"); ylabel("相位（原单位）");
title(sprintf("软件通道 %d（1 Hz高通，%d帧，%.2f秒）", ...
    channelNo, frameCount, t(end)));
