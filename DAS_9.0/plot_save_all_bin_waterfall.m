function [waterfallData, meta] = plot_save_all_bin_waterfall(folderPath, binIndex) % 定义主函数，用于解析保存全部数据中的指定 bin 并绘制距离-时间图
% 这个函数按 Qt 的保存逻辑读取指定的单个 bin 文件，并对每一帧每一行在 cols 方向计算标准差，再绘制瀑布图。 % 说明函数用途

if nargin < 1 || isempty(folderPath) % 如果没有传入文件夹路径
    folderPath = uigetdir(pwd, '请选择保存全部数据文件夹'); % 弹出文件夹选择框让用户选择保存全部数据文件夹
end % 结束文件夹路径默认值判断

if nargin < 2 || isempty(binIndex) % 如果没有传入 bin 序号
    binIndex = 1; % 默认读取第 1 个 bin 文件
end % 结束 bin 序号默认值判断

meta = parse_saved_data_meta(folderPath); % 解析文件夹元数据和 bin 文件列表

if ~meta.valid % 如果元数据解析失败
    error('无法从文件夹名解析有效参数，请检查保存文件夹名称格式。'); % 抛出元数据解析失败错误
end % 结束元数据有效性判断

if ~meta.isAllData % 如果当前文件夹不是保存全部数据文件夹
    error('当前文件夹不是保存全部数据文件夹，请使用 data_all_ 开头的文件夹。'); % 抛出类型不匹配错误
end % 结束保存类型判断

if meta.binCount <= 0 % 如果文件夹里没有 bin 文件
    error('文件夹中没有找到任何 bin 文件：%s', meta.folderPath); % 抛出无 bin 文件错误
end % 结束 bin 文件存在性判断

if binIndex < 1 || binIndex > meta.binCount % 如果输入的 bin 序号超出有效范围
    error('binIndex 超出范围，当前文件夹共有 %d 个 bin 文件。', meta.binCount); % 抛出 bin 序号越界错误
end % 结束 bin 序号范围判断

selectedBinInfo = meta.binFiles(binIndex); % 取出目标 bin 文件的信息结构体
selectedBinPath = fullfile(meta.folderPath, selectedBinInfo.name); % 拼出目标 bin 文件的完整路径
bytesPerFloat = 4; % C++ 端写入的是 float32，因此每个采样点占 4 个字节
frameFloatCount = meta.rows * meta.cols; % 一帧完整数据包含 rows*cols 个 float32 采样点
frameByteCount = frameFloatCount * bytesPerFloat; % 一帧完整数据对应的字节数
fileBytes = selectedBinInfo.bytes; % 获取目标 bin 文件的字节大小

if mod(fileBytes, bytesPerFloat) ~= 0 % 如果文件大小不是 float32 对齐
    error('目标 bin 文件大小不是 float32 对齐，无法正确解析：%s', selectedBinPath); % 抛出文件对齐错误
end % 结束 float32 对齐判断

if mod(fileBytes, frameByteCount) ~= 0 % 如果目标文件大小不是整帧字节数的整数倍
    error('目标 bin 文件大小与 rows*cols 不匹配，无法按帧解析：%s', selectedBinPath); % 抛出帧长度不匹配错误
end % 结束整帧匹配判断

frameCount = double(fileBytes / frameByteCount); % 计算该 bin 文件中包含多少帧完整数据
waterfallData = zeros(frameCount, meta.rows, 'single'); % 预分配用于保存每帧每行标准差的矩阵
fid = fopen(selectedBinPath, 'rb'); % 以只读二进制方式打开目标 bin 文件

if fid == -1 % 如果文件打开失败
    error('无法打开目标 bin 文件：%s', selectedBinPath); % 抛出文件打开失败错误
end % 结束文件打开判断

cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU> % 创建清理对象，确保函数退出时文件句柄一定会关闭

for frameIndex = 1 : frameCount % 逐帧读取该 bin 文件中的全部数据
    frameData = fread(fid, [meta.cols, meta.rows], 'float32=>double'); % 读取一帧数据，其中每一列对应一行位置数据
    if numel(frameData) ~= frameFloatCount % 如果当前帧读到的数据量不完整
        error('读取第 %d 帧时数据量不足，期望 %d 个点，实际 %d 个点。', frameIndex, frameFloatCount, numel(frameData)); % 抛出帧读取长度不足错误
    end % 结束当前帧长度判断
    frameStd = std(frameData, 0, 1); % 对每一列也就是每一行在 cols 方向计算标准差
    waterfallData(frameIndex, :) = single(frameStd); % 将该帧所有行的标准差保存到瀑布图矩阵中
end % 结束逐帧读取循环

if meta.intervalMeters > 0 % 如果文件夹参数中包含有效的物理距离间隔
    xAxis = meta.startMeters + (0 : meta.rows - 1) * meta.intervalMeters; % 生成横坐标距离轴，单位为米
    xLabelText = '距离 / m'; % 设置横坐标标签为距离
else % 如果没有有效的物理距离间隔
    xAxis = meta.startChannel + (0 : meta.rows - 1); % 退化为使用通道号作为横坐标
    xLabelText = '通道'; % 设置横坐标标签为通道
end % 结束横坐标类型判断

frameDurationSeconds = meta.cols / meta.frequency; % 计算每帧对应的时间长度，单位为秒
yAxis = (0 : frameCount - 1) * frameDurationSeconds; % 生成纵坐标时间轴，单位为秒

figure('Color', 'w', 'Name', '保存全部数据单个bin瀑布图'); % 创建新的绘图窗口并设置白色背景
imagesc(xAxis, yAxis, waterfallData); % 以图像形式绘制时间-距离矩阵
set(gca, 'YDir', 'normal'); % 将 Y 轴方向设置为从上到下递增显示
axis tight; % 让坐标轴范围紧贴数据边界
colormap(jet(256)); % 使用 jet 颜色表显示瀑布图
colorbarHandle = colorbar; % 创建颜色条并保存句柄
ylabel(colorbarHandle, 'STD'); % 设置颜色条标签为标准差
xlabel(xLabelText); % 设置横坐标标签
ylabel('时间 / s'); % 设置纵坐标标签
title(sprintf('保存全部数据 第%d个bin 瀑布图', binIndex)); % 设置图标题

fprintf('保存全部数据文件夹: %s\n', meta.folderPath); % 在命令行打印文件夹路径
fprintf('当前读取的 bin 序号: %d\n', binIndex); % 打印当前读取的 bin 序号
fprintf('当前读取的 bin 文件: %s\n', selectedBinPath); % 打印当前读取的 bin 文件路径
fprintf('采样频率 frequency: %d Hz\n', meta.frequency); % 打印采样频率
fprintf('抽取系数 extractCount: %d\n', meta.extractCount); % 打印抽取系数
fprintf('差分距离 differentialDistance: %d\n', meta.differentialDistance); % 打印差分距离参数
fprintf('每帧行数 rows: %d\n', meta.rows); % 打印每帧行数
fprintf('每行点数 cols: %d\n', meta.cols); % 打印每行点数
fprintf('当前 bin 包含帧数: %d\n', frameCount); % 打印当前 bin 中包含的完整帧数
fprintf('每帧时长: %.6f s\n', frameDurationSeconds); % 打印每帧对应的时间长度

if meta.extractCountInferred % 如果抽取系数是从简化格式文件夹名反推得到的
    fprintf('提示: extractCount 是根据 interval 和 frequency 反推得到的。\n'); % 打印抽取系数反推提示
end % 结束抽取系数反推提示判断

if meta.differentialDistanceInferred % 如果差分距离是从简化格式文件夹名反推得到的
    fprintf('提示: differentialDistance 是根据 pitch 和 frequency 反推得到的。\n'); % 打印差分距离反推提示
end % 结束差分距离反推提示判断

end % 结束保存全部数据单 bin 瀑布图函数
