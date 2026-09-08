# 三篇 DAS/φ-OTDR 论文中文学习摘要

## 1. Two-round feature selection combining with LightGBM classifier...

**中文摘要（学习版）：** 针对 φ-OTDR 系统误报率高的问题，论文引入 LightGBM 进行多类别扰动事件识别，并通过两轮特征选择去除冗余特征。特征数量由 40 个降至 14 个，识别时间由约 1 s 降至 0.3 s，可识别浇水、攀爬、敲击、按压和虚假扰动五类事件，平均准确率为 97.41%。

**方法重点：** 时域/差分特征 → 两轮特征筛选 → LightGBM 分类 → 事件报警。

## 2. Pipeline Safety Early Warning by Multifeature-Fusion CNN and LightGBM...

**中文摘要（学习版）：** 论文面向长距离油气管道第三方破坏预警，将分布式光纤传感器信号构造成多种时空特征，利用 CNN 提取深层特征，再与 LightGBM 的分类能力结合，实现事件识别和定位，并讨论强噪声、硬件变化和信号漂移条件下的实时部署。

**方法重点：** DAS 时空信号 → 多特征融合/CNN → LightGBM → 事件类别与位置输出。

## 3. φ-OTDR pattern recognition based on CNN-LSTM

**中文摘要（学习版）：** 论文将原始时域波形、离散小波变换结果和短时傅里叶变换（STFT）时频结果作为输入，使用 CNN 提取局部波形/时频结构，使用 LSTM 建模时间变化，从而识别六类目标振动信号，用于供水管线周边施工、行走、敲击及背景噪声等场景。

**方法重点：** 原始波形+DWT+STFT → CNN 局部特征 → LSTM 时序建模 → 六类事件识别。

> 以上是中文学习摘要，不是对受版权保护论文的全文翻译。需要逐段翻译时，可以提供论文 PDF 或指定页码/段落。
