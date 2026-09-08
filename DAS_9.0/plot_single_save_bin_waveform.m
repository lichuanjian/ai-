function [plotTimeAxis, plotWaveform, meta] = plot_single_save_bin_waveform(folderPath, binIndex) % 定义主函数，用于解析单点保存中的指定 bin 并绘制连续时域波形
% 这个函数按 Qt 单点保存查看器的思路读取指定 bin 文件，并将其中所有样本拼接成一条连续时域波形。 % 说明函数用途

if nargin < 1 || isempty(folderPath) % 如果没有传入文件夹路径
    folderPath = uigetdir(pwd, '请选择单点保存数据文件夹'); % 弹出文件夹选择框让用户选择单点保存数据文件夹
end % 结束文件夹路径默认值判断

if nargin < 2 || isempty(binIndex) % 如果没有传入 bin 序号
    binIndex = 1; % 默认读取第 1 个 bin 文件
end % 结束 bin 序号默认值判断

meta = parse_saved_data_meta(folderPath); % 解析文件夹元数据和 bin 文件列表

if ~meta.valid % 如果元数据解析失败
    error('无法从文件夹名解析有效参数，请检查保存文件夹名称格式。'); % 抛出元数据解析失败错误
end % 结束元数据有效性判断

if meta.isAllData % 如果当前文件夹其实是保存全部数据文件夹
    error('当前文件夹是保存全部数据文件夹，请改用单点保存文件夹，也就是 data_ 开头且不是 data_all_ 的文件夹。'); % 抛出类型不匹配错误
end % 结束保存类型判断

if meta.binCount <= 0 % 如果文件夹里没有任何 bin 文件
    error('文件夹中没有找到任何 bin 文件：%s', meta.folderPath); % 抛出无 bin 文件错误
end % 结束 bin 文件存在性判断

if binIndex < 1 || binIndex > meta.binCount % 如果输入的 bin 序号超出有效范围
    error('binIndex 超出范围，当前文件夹共有 %d 个 bin 文件。', meta.binCount); % 抛出 bin 序号越界错误
end % 结束 bin 序号范围判断

selectedBinInfo = meta.binFiles(binIndex); % 取出目标 bin 文件的信息结构体
selectedBinPath = fullfile(meta.folderPath, selectedBinInfo.name); % 拼出目标 bin 文件的完整路径
bytesPerFloat = 4; % C++ 端写入的是 float32，因此每个采样点占 4 个字节
fileBytes = selectedBinInfo.bytes; % 获取目标 bin 文件大小

if mod(fileBytes, bytesPerFloat) ~= 0 % 如果文件大小不是 float32 对齐
    error('目标 bin 文件大小不是 float32 对齐，无法正确解析：%s', selectedBinPath); % 抛出文件对齐错误
end % 结束 float32 对齐判断

if mod(fileBytes, meta.cols * bytesPerFloat) ~= 0 % 如果文件大小不是单点保存一帧长度的整数倍
    error('目标 bin 文件大小与单点保存的 cols 不匹配，无法按帧解析：%s', selectedBinPath); % 抛出帧长度不匹配错误
end % 结束整帧匹配判断

totalSamples = double(fileBytes / bytesPerFloat); % 计算当前 bin 文件中的总采样点数
frameCount = double(fileBytes / (meta.cols * bytesPerFloat)); % 计算当前 bin 文件中一共保存了多少帧单点数据
maxPlotSamples = 250000; % 设定用于绘图的最大样本点数上限，避免直接画几亿个点
plotStep = max(1, ceil(totalSamples / maxPlotSamples)); % 计算绘图抽样步长
estimatedPlotSamples = floor((totalSamples - 1) / plotStep) + 2; % 估计用于绘图的采样点数量并多留一个点给末尾样本
plotTimeAxis = zeros(1, estimatedPlotSamples); % 预分配绘图时间轴数组
plotWaveform = zeros(1, estimatedPlotSamples); % 预分配绘图波形数组
plotWriteIndex = 0; % 初始化绘图写入位置索引
sampleIndex = 0; % 初始化全局样本计数器，采用从 0 开始的计数方式
lastSampleValue = 0; % 初始化最后一个样本值
chunkFloatCount = 1024 * 1024; % 定义每次分块读取的 float32 样本个数
fid = fopen(selectedBinPath, 'rb'); % 以只读二进制方式打开目标 bin 文件

if fid == -1 % 如果文件打开失败
    error('无法打开目标 bin 文件：%s', selectedBinPath); % 抛出文件打开失败错误
end % 结束文件打开判断

cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU> % 创建清理对象，确保函数退出时文件句柄一定会关闭

while true % 循环分块读取整个 bin 文件
    chunkData = fread(fid, [1, chunkFloatCount], 'float32=>double'); % 分块读取 float32 数据并转换为 double
    if isempty(chunkData) % 如果已经读到文件末尾
        break; % 跳出循环
    end % 结束文件末尾判断

    chunkCount = numel(chunkData); % 获取当前分块读到的样本数量
    globalIndices = sampleIndex + (0 : chunkCount - 1); % 生成当前分块所有样本在整条时序中的全局下标
    selectMask = mod(globalIndices, plotStep) == 0; % 选出需要保留用于绘图的样本位置
    selectedCount = sum(selectMask); % 统计当前分块中被保留下来的样本个数

    if selectedCount > 0 % 如果当前分块中有样本需要被保存用于绘图
        writeRange = plotWriteIndex + (1 : selectedCount); % 计算本次写入到绘图数组中的位置范围
        plotTimeAxis(writeRange) = globalIndices(selectMask) / meta.frequency; % 将被选中的样本转换为时间轴坐标
        plotWaveform(writeRange) = chunkData(selectMask); % 将被选中的波形值写入绘图数组
        plotWriteIndex = plotWriteIndex + selectedCount; % 更新绘图数组当前写入位置
    end % 结束当前分块写入判断

    lastSampleValue = chunkData(end); % 保存当前分块的最后一个样本值，后面用于保证尾点被画出来
    sampleIndex = sampleIndex + chunkCount; % 更新全局样本计数器
end % 结束分块读取循环

if totalSamples <= 0 % 如果总样本数为 0
    error('目标 bin 文件中没有任何有效数据。'); % 抛出空数据错误
end % 结束空数据判断

lastSampleTime = (totalSamples - 1) / meta.frequency; % 计算最后一个样本对应的时间坐标

if plotWriteIndex == 0 || abs(plotTimeAxis(plotWriteIndex) - lastSampleTime) > eps(max(1, lastSampleTime)) % 如果当前绘图数据中还没有包含最后一个样本
    plotWriteIndex = plotWriteIndex + 1; % 将写入索引前进一步
    plotTimeAxis(plotWriteIndex) = lastSampleTime; % 把最后一个样本的时间坐标写入绘图数组
    plotWaveform(plotWriteIndex) = lastSampleValue; % 把最后一个样本值写入绘图数组
end % 结束末尾样本补点判断

plotTimeAxis = plotTimeAxis(1 : plotWriteIndex); % 按实际使用长度截取时间轴数组
plotWaveform = plotWaveform(1 : plotWriteIndex); % 按实际使用长度截取波形数组

figure('Color', 'w', 'Name', '单点保存单个bin连续时域波形'); % 创建新的绘图窗口并设置白色背景
plot(plotTimeAxis, plotWaveform, 'b-', 'LineWidth', 1.0); % 绘制单点保存的连续时域波形
grid on; % 打开网格方便观察波形变化
xlabel('时间 / s'); % 设置横坐标标签为时间
ylabel('幅值'); % 设置纵坐标标签为幅值
title(sprintf('单点保存 第%d个bin 连续时域波形', binIndex)); % 设置图标题

fprintf('单点保存文件夹: %s\n', meta.folderPath); % 在命令行打印文件夹路径
fprintf('当前读取的 bin 序号: %d\n', binIndex); % 打印当前读取的 bin 序号
fprintf('当前读取的 bin 文件: %s\n', selectedBinPath); % 打印当前读取的 bin 文件路径
fprintf('采样频率 frequency: %d Hz\n', meta.frequency); % 打印采样频率
fprintf('抽取系数 extractCount: %d\n', meta.extractCount); % 打印抽取系数
fprintf('差分距离 differentialDistance: %d\n', meta.differentialDistance); % 打印差分距离参数
fprintf('rows: %d\n', meta.rows); % 打印保存文件夹记录的 rows 参数
fprintf('cols: %d\n', meta.cols); % 打印每帧单点数据长度
fprintf('当前 bin 中的帧数: %d\n', frameCount); % 打印当前 bin 中包含的单点帧数
fprintf('当前 bin 中的总样本数: %d\n', totalSamples); % 打印当前 bin 中的总样本数
fprintf('总时长: %.6f s\n', totalSamples / meta.frequency); % 打印当前 bin 对应的总时长
fprintf('绘图抽样步长: %d\n', plotStep); % 打印用于绘图的抽样步长

if meta.extractCountInferred % 如果抽取系数是从简化格式文件夹名反推得到的
    fprintf('提示: extractCount 是根据 interval 和 frequency 反推得到的。\n'); % 打印抽取系数反推提示
end % 结束抽取系数反推提示判断

if meta.differentialDistanceInferred % 如果差分距离是从简化格式文件夹名反推得到的
    fprintf('提示: differentialDistance 是根据 pitch 和 frequency 反推得到的。\n'); % 打印差分距离反推提示
end % 结束差分距离反推提示判断

end % 结束单点保存单 bin 连续时域波形函数
