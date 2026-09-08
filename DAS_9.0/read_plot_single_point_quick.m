function y = read_plot_single_point_quick(binFile, channelNo)
% 快速读取 DASBIN01，并绘制一个软件通道的 1 Hz 高通波形。
% 直接运行不带参数时，会弹窗选择 BIN 文件和通道号。

if nargin < 1 || isempty(binFile)
    [name, folder] = uigetfile('*.bin', '选择 DAS BIN 文件');
    if isequal(name, 0), y = []; return; end
    binFile = fullfile(folder, name);
end

fid = fopen(binFile, 'rb', 'ieee-le');
assert(fid >= 0, '无法打开文件：%s', binFile);
cleaner = onCleanup(@() fclose(fid)); %#ok<NASGU>

% 读取 256 字节文件头中的必要参数
magic = char(fread(fid, 8, '*char').');
headerBytes = fread(fid, 1, 'uint32=>double');
version = fread(fid, 1, 'uint32=>double');
saveMode = fread(fid, 1, 'uint32=>double');
sampleType = fread(fid, 1, 'uint32=>double');
cfg = fread(fid, 8, 'int32=>double');
assert(strcmp(magic, 'DASBIN01') && headerBytes == 256 && ...
    version == 1 && sampleType == 1, '不是支持的 DASBIN01/float32 文件。');

fs = cfg(1); rows = cfg(2); cols = cfg(3);
startChannel = cfg(6); savedChannel = cfg(8);

if saveMode == 1                         % 文件保存了全部通道
    if nargin < 2 || isempty(channelNo)
        a = inputdlg('输入软件通道号：', '选择单点', 1, {'200'});
        if isempty(a), y = []; return; end
        channelNo = str2double(a{1});
    end
    rowIndex = channelNo - startChannel; % 从 0 开始的文件内行号
    assert(rowIndex >= 0 && rowIndex < rows, '通道号不在保存范围内。');

    info = dir(binFile);
    frameBytes = rows * cols * 4;
    frameCount = (info.bytes - headerBytes) / frameBytes;
    assert(frameCount == floor(frameCount), 'BIN 数据不是完整帧。');

    % 只读取目标通道，避免把全部通道载入内存
    y = zeros(cols * frameCount, 1);
    for k = 1:frameCount
        offset = headerBytes + ((k-1)*rows*cols + rowIndex*cols) * 4;
        fseek(fid, offset, 'bof');
        y((k-1)*cols + (1:cols)) = fread(fid, cols, 'single=>double');
    end
else                                      % 文件本身就是单通道
    channelNo = savedChannel;
    fseek(fid, headerBytes, 'bof');
    y = fread(fid, inf, 'single=>double');
end

% 1 Hz 高通；需要 Signal Processing Toolbox
[b, a] = butter(4, 1/(fs/2), 'high');
y = filtfilt(b, a, y);
t = (0:numel(y)-1)' / fs;

figure('Color', 'w', 'Name', 'DAS 单点波形');
plot(t, y, 'b', 'LineWidth', 1); grid on;
xlabel('时间 / s'); ylabel('应变相关相位 / rad');
title(sprintf('软件通道 %d（1 Hz 高通）', channelNo));
[~, fileName, ext] = fileparts(binFile);
subtitle(sprintf('%s%s  |  fs=%g Hz  |  %d 点', ...
    fileName, ext, fs, numel(y)), 'Interpreter', 'none');
end
