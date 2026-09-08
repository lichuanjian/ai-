%% 无文件头 BIN：快速读取并绘制单点波形
file = "E:\data_2026_09_02_00_01_52_freq10000Hz_ext8_diff8_startCh0_endCh3072_rows3072_cols5000_pitch3.20m_interval3.20m_startM3.20_endM9833.60\data_2026_09_02_00_01_52_0.bin";
fs = 10000;       % 采样率 Hz
highpassHz = 1;   % 高通截止频率 Hz

% 按小端 float32 读取；13个5000点块会自动连成一个波形
fid = fopen(file, "rb", "ieee-le");
assert(fid >= 0, "无法打开 BIN 文件");
phase = fread(fid, inf, "single=>double");
fclose(fid);

% 去除低频漂移
[b, a] = butter(4, highpassHz/(fs/2), "high");
phase1Hz = filtfilt(b, a, phase);
t = (0:numel(phase1Hz)-1)'/fs;

% 绘图
figure("Color", "w");
plot(t, phase1Hz, "b"); grid on;
xlabel("时间 / s"); ylabel("相位（原单位）");
title(sprintf("单点波形（1 Hz 高通，%d 点，%.2f s）", ...
    numel(phase1Hz), t(end)));
