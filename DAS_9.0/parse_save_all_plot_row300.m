function [rowData, meta] = parse_save_all_plot_row300(folderPath, rowIndex, frameIndex) % 定义主函数，输入为保存数据文件夹路径、目标行号和目标帧号，输出为该行数据和解析出的元数据
% 这个脚本用于解析 save_data 保存的“全部数据”文件夹，并绘制第一个 bin 文件中某一帧指定行的时域数据。 % 说明脚本用途

if nargin < 1 || isempty(folderPath) % 如果没有传入文件夹路径
    folderPath = uigetdir(pwd, '请选择保存全部数据的文件夹'); % 弹出文件夹选择框让用户选择数据文件夹
end % 结束文件夹路径判断

if isequal(folderPath, 0) % 如果用户取消了文件夹选择
    error('未选择数据文件夹，程序已停止。'); % 抛出错误并停止执行
end % 结束取消选择判断

if nargin < 2 || isempty(rowIndex) % 如果没有传入目标行号
    rowIndex = 300; % 默认绘制第 300 行
end % 结束行号默认值判断

if nargin < 3 || isempty(frameIndex) % 如果没有传入目标帧号
    frameIndex = 1; % 默认读取第 1 帧
end % 结束帧号默认值判断

folderPath = char(folderPath); % 将文件夹路径转换为字符数组，便于后续 MATLAB 函数处理
folderPath = strtrim(folderPath); % 去掉路径字符串前后的空白字符
folderPath = regexprep(folderPath, '[\\/]+$', ''); % 去掉路径末尾可能多余的斜杠或反斜杠

if ~isfolder(folderPath) % 如果给定路径不是有效文件夹
    error('指定路径不是有效文件夹：%s', folderPath); % 抛出路径无效错误
end % 结束文件夹有效性判断

[~, folderName, ~] = fileparts(folderPath); % 从完整路径中取出文件夹名
meta = parse_saved_folder_meta(folderName, folderPath); % 从文件夹名中解析频率、抽取系数、rows 和 cols 等参数

if ~meta.valid % 如果文件夹名不符合保存格式
    error('无法从文件夹名解析参数，请确认文件夹名符合 save_data 的保存规则。'); % 抛出解析失败错误
end % 结束元数据有效性判断

if ~meta.isAllData % 如果当前文件夹不是“保存全部数据”类型
    warning('当前文件夹前缀不是 data_all_，但仍会按全部数据格式解析。'); % 给出提醒但继续执行
end % 结束保存类型判断

binFiles = dir(fullfile(folderPath, '*.bin')); % 查找该文件夹下所有 bin 文件

if isempty(binFiles) % 如果没有找到 bin 文件
    error('文件夹下没有找到 bin 文件：%s', folderPath); % 抛出无 bin 文件错误
end % 结束 bin 文件存在性判断

binFiles = sort_bin_files_by_index(binFiles); % 按文件名尾部的数字索引对 bin 文件进行排序
firstBinPath = fullfile(folderPath, binFiles(1).name); % 取排序后的第一个 bin 文件完整路径

bytesPerFloat = 4; % C++ 端使用的是 float32，因此每个采样点占 4 个字节
frameFloatCount = meta.rows * meta.cols; % 一帧数据总共有 rows*cols 个 float 点
frameByteCount = frameFloatCount * bytesPerFloat; % 一帧数据的总字节数
firstBinInfo = dir(firstBinPath); % 读取第一个 bin 文件的信息
fileBytes = firstBinInfo.bytes; % 获取第一个 bin 文件的字节大小

if mod(fileBytes, bytesPerFloat) ~= 0 % 如果文件字节数不是 float32 的整数倍
    error('bin 文件大小不是 float32 对齐，无法正确解析：%s', firstBinPath); % 抛出文件对齐错误
end % 结束 float32 对齐判断

totalFloatCount = fileBytes / bytesPerFloat; % 计算第一个 bin 文件中总共有多少个 float32 采样点

if mod(totalFloatCount, frameFloatCount) ~= 0 % 如果文件中的总点数不是整帧大小的整数倍
    error('bin 文件大小与 rows*cols 不匹配，无法按帧解析：%s', firstBinPath); % 抛出帧长度不匹配错误
end % 结束整帧匹配判断

frameCountInFirstBin = totalFloatCount / frameFloatCount; % 计算第一个 bin 文件中包含多少帧完整数据

if rowIndex < 1 || rowIndex > meta.rows % 如果目标行号超出了有效范围
    error('目标行号超出范围，允许范围是 1 到 %d。', meta.rows); % 抛出行号越界错误
end % 结束行号范围判断

if frameIndex < 1 || frameIndex > frameCountInFirstBin % 如果目标帧号超出了第一个 bin 文件中的帧数范围
    error('目标帧号超出范围，第一个 bin 文件中共有 %d 帧。', frameCountInFirstBin); % 抛出帧号越界错误
end % 结束帧号范围判断

fid = fopen(firstBinPath, 'rb'); % 以只读二进制方式打开第一个 bin 文件

if fid == -1 % 如果文件打开失败
    error('无法打开 bin 文件：%s', firstBinPath); % 抛出文件打开失败错误
end % 结束文件打开判断

cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU> % 创建清理对象，确保函数结束时文件句柄一定会关闭

rowOffsetFloat = (frameIndex - 1) * frameFloatCount + (rowIndex - 1) * meta.cols; % 计算目标帧中目标行起始位置对应的 float 偏移量
rowOffsetByte = rowOffsetFloat * bytesPerFloat; % 将 float 偏移量转换成字节偏移量
seekStatus = fseek(fid, rowOffsetByte, 'bof'); % 将文件指针移动到目标行的起始位置

if seekStatus ~= 0 % 如果文件指针移动失败
    error('fseek 定位失败，无法跳转到目标行数据位置。'); % 抛出定位失败错误
end % 结束文件定位判断

rowData = fread(fid, [1, meta.cols], 'float32=>double'); % 按 float32 格式读取一整行数据，并转换成 double 便于 MATLAB 后续处理

if numel(rowData) ~= meta.cols % 如果实际读取到的点数不足一整行
    error('读取到的点数不足一整行，期望 %d 点，实际 %d 点。', meta.cols, numel(rowData)); % 抛出读取长度不足错误
end % 结束读取长度判断

timeAxis = (0 : meta.cols - 1) / meta.frequency; % 根据采样频率生成该行数据对应的时间轴，单位为秒

figure('Color', 'w', 'Name', '第300行时域数据'); % 新建绘图窗口并设置背景和窗口名
plot(timeAxis, rowData, 'b-', 'LineWidth', 1.2); % 绘制该行的时域波形
grid on; % 打开网格以便观察波形细节
xlabel('时间 / s'); % 设置横坐标标签
ylabel('幅值'); % 设置纵坐标标签
title(sprintf('第一个bin文件 第%d帧 第%d行时域数据', frameIndex, rowIndex)); % 设置图标题

fprintf('文件夹名称: %s\n', folderName); % 在命令行打印文件夹名称
fprintf('第一个 bin 文件: %s\n', firstBinPath); % 在命令行打印第一个 bin 文件路径
fprintf('采样频率 frequency: %d Hz\n', meta.frequency); % 打印采样频率
fprintf('抽取系数 extractCount: %d\n', meta.extractCount); % 打印抽取系数
fprintf('差分距离 differentialDistance: %d\n', meta.differentialDistance); % 打印差分距离参数
fprintf('每帧行数 rows: %d\n', meta.rows); % 打印每帧的行数
fprintf('每行点数 cols: %d\n', meta.cols); % 打印每行的点数
fprintf('第一个 bin 文件中的完整帧数: %d\n', frameCountInFirstBin); % 打印第一个 bin 文件中的完整帧数
fprintf('当前绘制的是第 %d 帧、第 %d 行。\n', frameIndex, rowIndex); % 打印当前绘图对应的帧号和行号

end % 结束主函数

function meta = parse_saved_folder_meta(folderName, folderPath) % 定义局部函数，用于从保存文件夹名称中解析参数
meta = struct(); % 创建结构体用于保存解析结果
meta.valid = false; % 先将有效标志初始化为 false
meta.folderName = folderName; % 保存文件夹名称
meta.folderPath = folderPath; % 保存文件夹完整路径
meta.isAllData = false; % 先将“是否全部数据”标志初始化为 false
meta.timestamp = ''; % 初始化时间戳字段
meta.frequency = 0; % 初始化频率字段
meta.extractCount = 1; % 初始化抽取系数字段
meta.differentialDistance = 1; % 初始化差分距离字段
meta.startChannel = 0; % 初始化起始通道字段
meta.endChannel = 0; % 初始化结束通道字段
meta.rows = 0; % 初始化行数字段
meta.cols = 0; % 初始化列数字段
meta.pitchMeters = 0; % 初始化差分距离对应米数字段
meta.intervalMeters = 0; % 初始化行间距米数字段
meta.startMeters = 0; % 初始化起始距离米数字段
meta.endMeters = 0; % 初始化结束距离米数字段
meta.extractCountInferred = false; % 初始化抽取系数是否为反推得到的标志
meta.differentialDistanceInferred = false; % 初始化差分距离系数是否为反推得到的标志
meta.channelRangeInferred = false; % 初始化起止通道是否为反推得到的标志

patternFull = ['^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})', ... % 构造完整格式正则表达式的前半部分，用于匹配前缀和时间戳
               '_freq(\d+)Hz_ext(\d+)_diff(\d+)_startCh(\d+)_endCh(\d+)', ... % 构造完整格式正则表达式的中间部分，用于匹配频率、抽取系数和通道范围
               '_rows(\d+)_cols(\d+)_pitch([-0-9.]+)m_interval([-0-9.]+)m', ... % 构造完整格式正则表达式的后半部分，用于匹配 rows、cols、pitch 和 interval
               '_startM([-0-9.]+)_endM([-0-9.]+)$']; % 构造完整格式正则表达式最后一部分，用于匹配起止距离

patternCompact = ['^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})', ... % 构造简化格式正则表达式的前半部分，用于匹配前缀和时间戳
                  '_freq(\d+)Hz_rows(\d+)_cols(\d+)', ... % 构造简化格式正则表达式中 rows 和 cols 的匹配部分
                  '_pitch([-0-9.]+)_end([-0-9.]+)_start([-0-9.]+)_interval([-0-9.]+)$']; % 构造简化格式正则表达式最后一部分，用于匹配距离参数

tokens = regexp(folderName, patternFull, 'tokens', 'once'); % 先尝试使用完整格式正则表达式从文件夹名中抓取所有参数

if ~isempty(tokens) % 如果完整格式匹配成功
    meta.isAllData = strcmp(tokens{1}, 'data_all_'); % 根据前缀判断是否为“保存全部数据”文件夹
    meta.timestamp = tokens{2}; % 记录保存时间戳
    meta.frequency = str2double(tokens{3}); % 解析采样频率
    meta.extractCount = str2double(tokens{4}); % 解析抽取系数
    meta.differentialDistance = str2double(tokens{5}); % 解析差分距离系数
    meta.startChannel = str2double(tokens{6}); % 解析起始通道
    meta.endChannel = str2double(tokens{7}); % 解析结束通道
    meta.rows = str2double(tokens{8}); % 解析每帧行数
    meta.cols = str2double(tokens{9}); % 解析每行点数
    meta.pitchMeters = str2double(tokens{10}); % 解析差分距离对应的米数
    meta.intervalMeters = str2double(tokens{11}); % 解析相邻行之间的物理间距
    meta.startMeters = str2double(tokens{12}); % 解析起始距离
    meta.endMeters = str2double(tokens{13}); % 解析结束距离
    meta.valid = meta.frequency > 0 && meta.rows > 0 && meta.cols > 0; % 只有频率、行数和列数都有效时才认为解析成功
    return; % 完整格式解析成功后直接返回
end % 结束完整格式匹配判断

tokens = regexp(folderName, patternCompact, 'tokens', 'once'); % 如果完整格式失败，则尝试用简化格式正则表达式解析

if isempty(tokens) % 如果简化格式也没有成功匹配到参数
    return; % 直接返回并保持 valid=false
end % 结束简化格式匹配判断

meta.isAllData = strcmp(tokens{1}, 'data_all_'); % 根据前缀判断是否为“保存全部数据”文件夹
meta.timestamp = tokens{2}; % 记录保存时间戳
meta.frequency = str2double(tokens{3}); % 解析采样频率
meta.rows = str2double(tokens{4}); % 解析每帧行数
meta.cols = str2double(tokens{5}); % 解析每行点数
meta.pitchMeters = str2double(tokens{6}); % 解析差分距离对应的米数
meta.endMeters = str2double(tokens{7}); % 解析结束距离
meta.startMeters = str2double(tokens{8}); % 解析起始距离
meta.intervalMeters = str2double(tokens{9}); % 解析相邻行之间的物理间距

meterPerRawPoint = meter_per_raw_point_for_frequency(meta.frequency); % 根据频率获取单个原始点对应的物理距离

if meterPerRawPoint > 0 && meta.intervalMeters > 0 % 如果能拿到有效的原始点距离和 interval
    meta.extractCount = max(1, round(meta.intervalMeters / meterPerRawPoint)); % 根据 interval 反推抽取系数
    meta.extractCountInferred = true; % 标记抽取系数为反推得到
end % 结束抽取系数反推判断

if meterPerRawPoint > 0 && meta.pitchMeters > 0 % 如果能拿到有效的原始点距离和 pitch
    meta.differentialDistance = max(1, round(meta.pitchMeters / meterPerRawPoint)); % 根据 pitch 反推差分距离系数
    meta.differentialDistanceInferred = true; % 标记差分距离系数为反推得到
end % 结束差分距离系数反推判断

if meta.intervalMeters > 0 % 如果存在有效的行间距
    meta.startChannel = max(0, round(meta.startMeters / meta.intervalMeters)); % 根据起始距离和行间距反推起始通道
    meta.endChannel = max(meta.startChannel, round(meta.endMeters / meta.intervalMeters)); % 根据结束距离和行间距反推结束通道
    meta.channelRangeInferred = true; % 标记起止通道为反推得到
end % 结束起止通道反推判断

meta.valid = meta.frequency > 0 && meta.rows > 0 && meta.cols > 0; % 对简化格式同样要求频率、行数和列数有效

end % 结束元数据解析函数

function meterPerRawPoint = meter_per_raw_point_for_frequency(frequency) % 定义局部函数，用于复现 C++ 里按频率映射原始点距离的规则
meterPerRawPoint = 0.4; % 默认情况下原始点间距为 0.4 米

if frequency == 3333 % 如果频率为 3333Hz
    meterPerRawPoint = 1.2; % 对应每个原始点 1.2 米
elseif frequency == 2000 % 如果频率为 2000Hz
    meterPerRawPoint = 2.0; % 对应每个原始点 2.0 米
else % 对于其他项目代码里支持的频率
    meterPerRawPoint = 0.4; % 对应每个原始点 0.4 米
end % 结束频率判断

end % 结束原始点距离映射函数

function sortedFiles = sort_bin_files_by_index(binFiles) % 定义局部函数，用于将 bin 文件按照尾部索引排序
fileNames = {binFiles.name}; % 取出所有 bin 文件名
fileIndex = zeros(size(fileNames)); % 预分配用于保存每个文件尾部索引的数组

for k = 1 : numel(fileNames) % 遍历所有文件名
    token = regexp(fileNames{k}, '_(\d+)\.bin$', 'tokens', 'once'); % 提取文件名末尾的数字索引
    if isempty(token) % 如果没有匹配到索引
        fileIndex(k) = 0; % 默认将索引视为 0
    else % 如果成功匹配到索引
        fileIndex(k) = str2double(token{1}); % 将索引字符串转换为数值
    end % 结束索引匹配判断
end % 结束文件名遍历

[~, sortOrder] = sort(fileIndex); % 按照索引从小到大生成排序顺序
sortedFiles = binFiles(sortOrder); % 根据排序顺序重新排列 bin 文件列表

end % 结束 bin 文件排序函数
