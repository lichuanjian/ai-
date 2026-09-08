# MATLAB 读取单点保存数据说明

本文档对应脚本：

```matlab
matlab/read_single_save_bin.m
```

脚本已按 MATLAB 2014 兼容写法处理：不使用 `string`、`isfolder`、`highpass` 等新版函数；读取使用老版本支持的 `fread(..., '*single')`；滤波默认使用 IIR。

## 1. 这个脚本读取什么数据

`sava_data.cpp` 的“保存单点”会创建类似下面的文件夹：

```text
data_2026_05_14_20_07_04_freq10000Hz_ext1_diff16_startCh0_endCh750_rows750_cols5000_pitch6.40m_interval0.40m_startM0.00_endM300.00
```

文件夹中的 `.bin` 文件没有文件头，内容是连续写入的 `float32` 小端数据。保存逻辑为：

```text
每收到一帧数据 -> 取被锁定的某一个通道 -> 写入 cols 个 float32 点
多个帧顺序拼接；文件超过 2GB 时自动切成 _0.bin、_1.bin、...
```

所以 MATLAB 读取时需要从文件夹名解析：

- `freq10000Hz`：采样频率，单位 Hz。
- `cols5000`：每次写入的单点采样长度。
- `rows750`：采集时的通道数，仅用于校验和记录。
- `ext/diff/pitch/interval/startM/endM`：空间和差分参数，用于记录元数据。

注意：当前 C++ 保存文件夹名没有记录“单点保存时锁定的是第几个通道”。如果后续需要严格知道通道号，建议在 C++ 保存时把 `active_single_save_row` 也写进文件夹名或写一个 metadata.txt。

## 2. 最简单用法

最简就是两行代码：

```matlab
addpath('D:\DAS_9.0(1)\DAS_9.0\matlab');
read_single_save_bin('E:\你的单点保存文件夹\data_2026_xx_xx_xx_xx_xx_freq10000Hz_ext1_diff16_startCh0_endCh750_rows750_cols5000_pitch6.40m_interval0.40m_startM0.00_endM300.00');
```

第一行：让 MATLAB 找到 `read_single_save_bin.m` 函数。

第二行：输入你的单点保存文件夹路径，函数会自动读取 bin、解析文件夹名参数、做 1 Hz IIR 高通滤波并显示波形。

如果你想把结果也保存到 MATLAB 变量里，再写成：

```matlab
addpath('D:\DAS_9.0(1)\DAS_9.0\matlab');
result = read_single_save_bin('E:\你的单点保存文件夹\data_2026_xx_xx_xx_xx_xx_freq10000Hz_ext1_diff16_startCh0_endCh750_rows750_cols5000_pitch6.40m_interval0.40m_startM0.00_endM300.00');
```

默认行为：

- 自动读取该文件夹下所有 `.bin`，并按 `_0.bin`、`_1.bin` 的序号排序拼接。
- 自动从文件夹名解析采样频率等参数。
- 对完整单点波形做 `1 Hz` 高通滤波。
- 绘制两幅图：原始波形、1 Hz 高通后的波形。

## 3. 返回结果怎么用

函数返回一个结构体 `result`：

```matlab
result.time       % 时间轴，单位 s
result.raw        % 原始单点相位/幅值序列，single
result.filtered   % 1 Hz 高通后的序列，single
result.meta       % 从文件夹名解析出来的参数
result.filter     % 滤波方式、截止频率、采样率
```

例如单独画高通后的波形：

```matlab
plot(result.time, result.filtered);
grid on;
xlabel('Time / s');
ylabel('Phase after 1 Hz high-pass');
```

查看解析出来的采样率：

```matlab
result.meta.frequency
```

查看总时长：

```matlab
result.durationSeconds
```

## 4. 只读取某一个 bin

如果文件夹很大，可以只读取第 1 个 bin：

```matlab
result = read_single_save_bin(folderPath, 'BinIndex', 1);
```

读取第 2 和第 3 个 bin：

```matlab
result = read_single_save_bin(folderPath, 'BinIndex', [2 3]);
```

注意：这里 `BinIndex=1` 表示排序后的第 1 个文件，通常对应文件名 `_0.bin`。

## 5. 保存为 MAT 文件

如果希望读取和滤波后保存成 `.mat`：

```matlab
result = read_single_save_bin(folderPath, 'SaveMat', true);
```

默认会保存到原始数据文件夹：

```text
single_point_1Hz_highpass.mat
```

也可以指定输出路径：

```matlab
result = read_single_save_bin(folderPath, ...
    'SaveMat', true, ...
    'OutputMatPath', 'D:\temp\single_point_result.mat');
```

## 6. 大文件注意事项

脚本默认最多一次读取约 `8 GB` 的 float32 原始数据：

```matlab
result = read_single_save_bin(folderPath, 'MaxLoadGB', 8);
```

如果超过限制，函数会报错提醒。可以选择：

```matlab
% 只读取一个 bin
result = read_single_save_bin(folderPath, 'BinIndex', 1);

% 或者机器内存足够时提高上限
result = read_single_save_bin(folderPath, 'MaxLoadGB', 16);
```

绘图时不会把所有点都画出来，默认最多抽样显示 `250000` 个点，避免 MATLAB 卡死：

```matlab
result = read_single_save_bin(folderPath, 'MaxPlotPoints', 100000);
```

如果只想读取和滤波，不想画图：

```matlab
result = read_single_save_bin(folderPath, 'MakePlot', false);
```

## 7. 滤波说明

默认滤波参数：

```text
高通截止频率: 1 Hz
滤波器类型: 2 阶 Butterworth IIR 高通
滤波方向: 单向 causal IIR，使用稳态初值
```

脚本现在默认不用 `highpass/filtfilt` 做零相位滤波，而是使用 IIR 单向滤波。原因是 `filtfilt` 会在数据首尾做延拓，如果原始信号头部或尾部有突变、截断、异常点或很强的低频漂移，容易把边界不连续放大成首尾振铃。单向 IIR 会有一点相位延迟，但头尾更稳定，更适合先看单点保存波形。

如果安装了 Signal Processing Toolbox，脚本使用 `butter + filter`；如果没有 `butter`，则退化为一阶 RC IIR 高通。

修改截止频率示例：

```matlab
result = read_single_save_bin(folderPath, 'HighpassHz', 1);
```

修改 IIR 阶数示例：

```matlab
result = read_single_save_bin(folderPath, 'FilterOrder', 2);
```

如果你特别需要零相位滤波，可以打开 `UseZeroPhase`，但首尾可能再次出现明显振铃：

```matlab
result = read_single_save_bin(folderPath, 'UseZeroPhase', true);
```

如果需要原始数据不滤波：

```matlab
result = read_single_save_bin(folderPath, 'HighpassHz', 0);
```

## 8. 常见错误

### 选择了保存全部数据文件夹

如果文件夹名以 `data_all_` 开头，说明它是保存全部数据，不是单点保存。这个脚本会拒绝读取，并提示改用单点保存文件夹。

### 文件大小和 cols 不匹配

单点保存要求每个 bin 的字节数必须是：

```text
cols * 4 字节
```

的整数倍。如果不匹配，说明文件可能没有写完整、被截断，或文件夹名和 bin 文件不是同一次保存产生的。

### 不知道保存的是哪个通道

目前文件夹名没有记录单点通道号。MATLAB 只能读取数据和解析采样参数，不能从 bin 内反推出保存通道。建议下一步在 C++ 保存单点时新增：

```text
_singleRow500
```

或写入：

```text
metadata.txt
```

记录 `active_single_save_row`。
