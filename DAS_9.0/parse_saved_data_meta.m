function meta = parse_saved_data_meta(folderPath) % 定义公共函数，用于解析保存数据文件夹的元数据和 bin 文件列表
% 这个函数同时兼容保存全部数据和单点保存数据两种文件夹命名格式。 % 说明函数用途

if nargin < 1 || isempty(folderPath) % 如果没有传入文件夹路径
    folderPath = uigetdir(pwd, '请选择保存数据文件夹'); % 弹出文件夹选择框让用户选择数据文件夹
end % 结束文件夹路径默认值判断

if isequal(folderPath, 0) % 如果用户取消了文件夹选择
    error('未选择数据文件夹，程序已停止。'); % 抛出错误并停止执行
end % 结束取消文件夹选择判断

folderPath = char(folderPath); % 将文件夹路径转换为字符数组以便后续处理
folderPath = strtrim(folderPath); % 去掉路径前后的空白字符
folderPath = regexprep(folderPath, '[\\/]+$', ''); % 去掉路径末尾多余的斜杠或反斜杠

if ~isfolder(folderPath) % 如果给定路径不是有效文件夹
    error('指定路径不是有效文件夹：%s', folderPath); % 抛出无效文件夹错误
end % 结束文件夹有效性判断

[~, folderName, ~] = fileparts(folderPath); % 从完整路径中提取文件夹名称

meta = struct(); % 创建结构体保存解析结果
meta.valid = false; % 初始化有效标志为 false
meta.folderPath = folderPath; % 保存文件夹完整路径
meta.folderName = folderName; % 保存文件夹名称
meta.isAllData = false; % 初始化是否为保存全部数据的标志
meta.timestamp = ''; % 初始化时间戳字段
meta.frequency = 0; % 初始化采样频率字段
meta.extractCount = 1; % 初始化抽取系数字段
meta.differentialDistance = 1; % 初始化差分距离字段
meta.startChannel = 0; % 初始化起始通道字段
meta.endChannel = 0; % 初始化结束通道字段
meta.rows = 0; % 初始化行数字段
meta.cols = 0; % 初始化列数字段
meta.pitchMeters = 0; % 初始化差分距离对应的物理距离字段
meta.intervalMeters = 0; % 初始化相邻行之间的物理间距字段
meta.startMeters = 0; % 初始化起始距离字段
meta.endMeters = 0; % 初始化结束距离字段
meta.extractCountInferred = false; % 初始化抽取系数是否为反推值的标志
meta.differentialDistanceInferred = false; % 初始化差分距离是否为反推值的标志
meta.channelRangeInferred = false; % 初始化通道范围是否为反推值的标志
meta.binFiles = struct('name', {}, 'folder', {}, 'date', {}, 'bytes', {}, 'isdir', {}, 'datenum', {}); % 初始化 bin 文件列表
meta.binCount = 0; % 初始化 bin 文件个数字段

patternFull = ['^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})', ... % 构造完整格式的正则表达式前半部分
               '_freq(\d+)Hz_ext(\d+)_diff(\d+)_startCh(\d+)_endCh(\d+)', ... % 构造完整格式中频率、抽取系数和通道范围部分
               '_rows(\d+)_cols(\d+)_pitch([-0-9.]+)m_interval([-0-9.]+)m', ... % 构造完整格式中 rows、cols、pitch 和 interval 部分
               '_startM([-0-9.]+)_endM([-0-9.]+)$']; % 构造完整格式中起止距离部分

patternCompact = ['^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})', ... % 构造简化格式的正则表达式前半部分
                  '_freq(\d+)Hz_rows(\d+)_cols(\d+)', ... % 构造简化格式中频率、rows 和 cols 部分
                  '_pitch([-0-9.]+)_end([-0-9.]+)_start([-0-9.]+)_interval([-0-9.]+)$']; % 构造简化格式中的距离参数部分

tokens = regexp(folderName, patternFull, 'tokens', 'once'); % 先尝试按完整格式解析文件夹名

if ~isempty(tokens) % 如果完整格式解析成功
    meta.isAllData = strcmp(tokens{1}, 'data_all_'); % 根据前缀判断是否为保存全部数据
    meta.timestamp = tokens{2}; % 记录时间戳
    meta.frequency = str2double(tokens{3}); % 解析采样频率
    meta.extractCount = str2double(tokens{4}); % 解析抽取系数
    meta.differentialDistance = str2double(tokens{5}); % 解析差分距离
    meta.startChannel = str2double(tokens{6}); % 解析起始通道
    meta.endChannel = str2double(tokens{7}); % 解析结束通道
    meta.rows = str2double(tokens{8}); % 解析行数
    meta.cols = str2double(tokens{9}); % 解析列数
    meta.pitchMeters = str2double(tokens{10}); % 解析差分距离对应的米数
    meta.intervalMeters = str2double(tokens{11}); % 解析相邻行的物理间距
    meta.startMeters = str2double(tokens{12}); % 解析起始距离
    meta.endMeters = str2double(tokens{13}); % 解析结束距离
else % 如果完整格式解析失败
    tokens = regexp(folderName, patternCompact, 'tokens', 'once'); % 再尝试按简化格式解析文件夹名
    if ~isempty(tokens) % 如果简化格式解析成功
        meta.isAllData = strcmp(tokens{1}, 'data_all_'); % 根据前缀判断是否为保存全部数据
        meta.timestamp = tokens{2}; % 记录时间戳
        meta.frequency = str2double(tokens{3}); % 解析采样频率
        meta.rows = str2double(tokens{4}); % 解析行数
        meta.cols = str2double(tokens{5}); % 解析列数
        meta.pitchMeters = str2double(tokens{6}); % 解析差分距离对应的米数
        meta.endMeters = str2double(tokens{7}); % 解析结束距离
        meta.startMeters = str2double(tokens{8}); % 解析起始距离
        meta.intervalMeters = str2double(tokens{9}); % 解析相邻行的物理间距

        meterPerRawPoint = meter_per_raw_point_for_frequency(meta.frequency); % 根据频率计算每个原始点对应的物理距离

        if meterPerRawPoint > 0 && meta.intervalMeters > 0 % 如果频率和 interval 都有效
            meta.extractCount = max(1, round(meta.intervalMeters / meterPerRawPoint)); % 根据 interval 反推抽取系数
            meta.extractCountInferred = true; % 标记抽取系数是反推得到的
        end % 结束抽取系数反推判断

        if meterPerRawPoint > 0 && meta.pitchMeters > 0 % 如果频率和 pitch 都有效
            meta.differentialDistance = max(1, round(meta.pitchMeters / meterPerRawPoint)); % 根据 pitch 反推差分距离
            meta.differentialDistanceInferred = true; % 标记差分距离是反推得到的
        end % 结束差分距离反推判断

        if meta.intervalMeters > 0 % 如果 interval 有效
            meta.startChannel = max(0, round(meta.startMeters / meta.intervalMeters)); % 根据起始距离反推起始通道
            meta.endChannel = max(meta.startChannel, round(meta.endMeters / meta.intervalMeters)); % 根据结束距离反推结束通道
            meta.channelRangeInferred = true; % 标记通道范围是反推得到的
        end % 结束起止通道反推判断
    end % 结束简化格式解析成功判断
end % 结束文件夹名格式判断

meta.valid = meta.frequency > 0 && meta.rows > 0 && meta.cols > 0; % 只有频率、行数和列数都有效时才认为解析成功

binFiles = dir(fullfile(folderPath, '*.bin')); % 扫描文件夹下所有的 bin 文件
binFiles = sort_bin_files_by_index(binFiles); % 按文件名尾部索引对 bin 文件进行排序
meta.binFiles = binFiles; % 保存排序后的 bin 文件列表
meta.binCount = numel(binFiles); % 保存 bin 文件数量

end % 结束公共元数据解析函数

function meterPerRawPoint = meter_per_raw_point_for_frequency(frequency) % 定义局部函数，用于复现 Qt 中按频率映射原始点距离的规则
meterPerRawPoint = 0.4; % 默认频率下每个原始点对应 0.4 米

if frequency == 3333 % 如果频率是 3333Hz
    meterPerRawPoint = 1.2; % 每个原始点对应 1.2 米
elseif frequency == 2000 % 如果频率是 2000Hz
    meterPerRawPoint = 2.0; % 每个原始点对应 2.0 米
else % 对于其他频率
    meterPerRawPoint = 0.4; % 每个原始点对应 0.4 米
end % 结束频率判断

end % 结束原始点距离映射函数

function sortedFiles = sort_bin_files_by_index(binFiles) % 定义局部函数，用于按文件名末尾索引对 bin 文件排序
if isempty(binFiles) % 如果 bin 文件列表为空
    sortedFiles = binFiles; % 直接返回空列表
    return; % 结束函数
end % 结束空列表判断

fileNames = {binFiles.name}; % 取出所有文件名
fileIndex = zeros(size(fileNames)); % 预分配用于保存文件尾部索引的数组

for k = 1 : numel(fileNames) % 遍历所有文件名
    token = regexp(fileNames{k}, '_(\d+)\.bin$', 'tokens', 'once'); % 提取文件名结尾的数字索引
    if isempty(token) % 如果没有匹配到索引
        fileIndex(k) = 0; % 默认将该文件索引记为 0
    else % 如果成功匹配到索引
        fileIndex(k) = str2double(token{1}); % 将索引字符串转换成数字
    end % 结束索引匹配判断
end % 结束文件遍历

[~, sortOrder] = sort(fileIndex); % 按数字索引升序生成排序顺序
sortedFiles = binFiles(sortOrder); % 根据排序顺序重排文件列表

end % 结束 bin 文件排序函数
