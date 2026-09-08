/**
 * @file Filter.cpp
 * @brief 高通滤波、双监听行提取及 RMS/应变支路分发。
 *
 * Filter 为每个空间行维护独立的八阶 Chebyshev I 高通滤波器，避免不同位置共享 IIR 历史状态。
 * 有效滤波时按行分块并行处理，随后复制两个监测行给音频和 FFT；滤波关闭时改为均匀抽取五列，
 * 供应变/相位定位显示。频率、行数或截止频率改变会先安全重建滤波器组。
 */
#include <QThread>
#include "Filter.h"
#include <QAudioDevice>
#include <QAudioInput>
#include <QAudioSource>
#include <QDebug>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <functional>
#include <new>

// 模板实例化：确保ChebyshevI型8阶高通滤波器的编译（解决模板链接问题）
template class Iir::ChebyshevI::HighPass<>;

// 最终要传递的提取数据格式（列提取后的二维数据）
QList<QVector<float>> extractedData;

// 滤波工作结构体：移除padding相关冗余参数，直接操作原始数组
// 作用：封装并行滤波任务的参数和执行逻辑
struct FilterWorker {
    float* array;          // 原始数据数组指针（直接操作，无需额外填充数组）
    int cols;              // 数组列数（无padding，原始列数）
    int rowStart;          // 处理起始行
    int rowEnd;            // 处理结束行
    int audioLocation;     // 音频数据所在行位置
    bool isFilterEnabled=true;  // 滤波功能使能标志
    QVector<Iir::ChebyshevI::HighPass<8>*>* rowFilters; // 每行独立的8阶高通滤波器数组

    /**
     * @brief 保存一次并行滤波任务所需的行范围、源数组和每行滤波器。
     * @details 该轻量任务对象只记录调用方提供的引用/指针，不转移其所有权；
     *          后续 operator() 会在调用方已保证数据有效的前提下执行对应行段。
     */
    // 构造函数：初始化滤波任务参数（移除padding/totalCols冗余参数）
    FilterWorker(float* array, int cols, int rowStart, int rowEnd,
                 int audioLocation, bool isFilterEnabled,
                 QVector<Iir::ChebyshevI::HighPass<8>*>* rowFilters)
        : array(array),
        cols(cols),
        rowStart(rowStart),
        rowEnd(rowEnd),
        audioLocation(audioLocation),
        isFilterEnabled(isFilterEnabled),
        rowFilters(rowFilters)
    {}

    // 重载()运算符：执行具体的滤波任务
/**
 * @brief 执行该实现单元中定义的业务步骤，并维持既有数据流与状态约束。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    void operator()() {
        // 仅在滤波使能时执行滤波逻辑
        if (isFilterEnabled) {
            // 遍历指定行范围，每行使用独立的滤波器处理
            for (int row = rowStart; row < rowEnd; row++) {
                // 获取当前行对应的滤波器
                Iir::ChebyshevI::HighPass<8>* filter = rowFilters->at(row);
                // 滤波器为空则跳过当前行
                if (!filter) continue;

                // 计算当前行数据在原始数组中的起始地址
                float* currentRow = array + row * cols;

                // 对当前行的所有列数据执行滤波
                for (int i = 0; i < cols; i++) {
                    currentRow[i] = filter->filter(currentRow[i]);
                }
            }
        }
    }
};

// 析构函数：清理所有行滤波器（内存安全，防止内存泄漏）
/**
 * @brief 完成对象析构阶段的收尾释放。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
Filter::~Filter() {
    clearRowFilters();
}

// 创建每行独立的切比雪夫I型高通滤波器（内存安全，仅参数变化时重建）
// 参数：rows-数组行数 frequency-采样率
/**
 * @brief 建立本模块运行所需的资源、状态或关联对象。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void Filter::createRowFilters(int rows, int frequency) {
    // 先清理旧的滤波器，避免内存泄漏
   clearRowFilters();

    if (rows <= 0) {
        m_lastRows = 0;
        return;
    }

    const int safeFrequency = qMax(1, frequency);
    const double nyquist = static_cast<double>(safeFrequency) * 0.5;
    const double safeCutoffHz = qBound(0.1, m_cutoffHz, qMax(0.1, nyquist - 0.1));

    // IIR 库在无效参数下会触发断言并导致 abort。
    if (nyquist <= safeCutoffHz) {
        qWarning() << "createRowFilters skipped: invalid sample rate for cutoff."
                   << "frequency=" << safeFrequency
                   << "cutoff=" << safeCutoffHz;
        m_rowFilters.fill(nullptr, rows);
        m_lastRows = rows;
        return;
    }

    // 调整滤波器数组大小，每行对应一个独立滤波器（参数独立）
    m_rowFilters.resize(rows);
    for (int i = 0; i < rows; i++) {
        // 创建8阶切比雪夫I型高通滤波器实例
        auto* filter = new(std::nothrow) Iir::ChebyshevI::HighPass<8>();
        if (!filter) {
            qWarning() << "createRowFilters allocation failed at row" << i;
            m_rowFilters[i] = nullptr;
            continue;
        }
        // 配置滤波器参数：
        // 参数1：阶数（必须与模板<8>一致）
        // 参数2：采样率（Hz）
        // 参数3：高通截止频率（Hz，由界面设置）
        // 参数4：纹波系数（0.1dB通带纹波）
        filter->setup(8, safeFrequency, safeCutoffHz, 0.1);
        // 重置滤波器状态（清除历史缓存）
        filter->reset();
        // 将滤波器存入数组
        m_rowFilters[i] = filter;
    }

    // 更新配置记录：记录当前行数（用于判断是否需要重建滤波器）
    m_lastRows = rows;
    m_frequency = safeFrequency;
    m_filterBankDirty = false;
}

// 清理所有行滤波器（内存管理，防止内存泄漏）
/**
 * @brief 清理或复位当前模块维护的状态与资源。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void Filter::clearRowFilters() {
    // 批量删除所有滤波器实例
    qDeleteAll(m_rowFilters);
    // 清空滤波器数组
    m_rowFilters.clear();
    // 重置配置记录
    m_lastRows = 0;
}

// 接收滤波使能状态信号（外部控制滤波功能开关）
// 参数：ok-true启用滤波 false禁用滤波
/**
 * @brief 执行该实现单元中定义的业务步骤，并维持既有数据流与状态约束。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void Filter::Receive_is_filter(bool ok) {
    const bool changed = (m_isFilterEnabled != ok);
    m_isFilterEnabled = ok;
    if (changed && m_isFilterEnabled) {
        for (auto *filter : m_rowFilters) {
            if (filter) filter->reset();
        }
    }
    qDebug() << "音频行滤波功能状态：" << (m_isFilterEnabled ? "启用" : "禁用");
}

// 构造函数：初始化参数和信号连接（内存安全，参数初始化）
/**
 * @brief 执行本函数负责的计算或数据变换步骤。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
Filter::Filter(QObject *parent)
    : QObject(parent),
    m_audioLocation(0),          // 音频行位置初始值
    m_isFilterEnabled(true),    // 默认启用滤波
    m_frequency(0),              // 采样率初始值
    m_lastRows(0)                // 上次行数初始值
{
    // 连接参数管理器的参数变化信号：监听参数更新
    connect(&ParameterManager::instance(), &ParameterManager::parameterChanged,
            this, &Filter::onParameterChanged);

    // 初始化监听行位置参数（从参数管理器读取）
    QVariant monitorVar = ParameterManager::instance().getParameter("monitorPosition");
    bool ok;
    m_audioLocation = monitorVar.toInt(&ok);
    // 读取失败时使用默认值0
    if (!ok) m_audioLocation = 200;

    // 初始化双音频行位置（备用音频通道）
    m_audioLocations[0] = ParameterManager::instance().getParameter("monitorPosition1").toInt();
    m_audioLocations[1] = ParameterManager::instance().getParameter("monitorPosition2").toInt();
    // 位置无效时使用默认值（主音频行+偏移）
    if (m_audioLocations[0] < 0) m_audioLocations[0] = 200;
    if (m_audioLocations[1] < 0) m_audioLocations[1] = 300;

    bool cutoffOk = false;
    const double configuredCutoff = ParameterManager::instance().getParameter("high_pass_cutoff_hz").toDouble(&cutoffOk);
    m_cutoffHz = cutoffOk && configuredCutoff > 0.0 ? configuredCutoff : 1.0;
}

// 参数变化处理槽函数：响应参数管理器的参数更新
// 参数：key-参数名 value-新参数值
/**
 * @brief 响应异步事件或上游数据到达，并将结果交给当前模块的后续处理流程。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void Filter::onParameterChanged(const QString& key, const QVariant& value) {
    // 更新单音频监听行位置
    if (key == "monitorPosition") {
        m_audioLocation = value.toInt();
        qDebug() << "单音频监听行位置已更新为：" << m_audioLocation;
    }
    // 更新第一路音频监听行位置
    else if (key == "monitorPosition1") {
        m_audioLocations[0] = value.toInt();
    }
    // 更新第二路音频监听行位置
    else if (key == "monitorPosition2") {
        m_audioLocations[1] = value.toInt();
    }
    else if (key == "high_pass_cutoff_hz") {
        bool ok = false;
        const double cutoff = value.toDouble(&ok);
        if (ok && cutoff > 0.0) {
            m_cutoffHz = cutoff;
            m_filterBankDirty = true;
            clearRowFilters();
            qDebug() << "高通截止频率已更新为：" << m_cutoffHz << "Hz";
        }
    }
}

// 核心数据处理槽函数：简化padding逻辑，直接处理原始数组，并行滤波+数据提取
// 参数：
// array-原始数据数组指针
// rows-数组行数
// cols-数组列数
// frequency-采样率
// extractCount-数据提取计数（用于标记批次）
/**
 * @brief 响应异步事件或上游数据到达，并将结果交给当前模块的后续处理流程。
 * @details 此实现属于滤波工作线程与参数协调层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void Filter::array_handleSignal(float* array, int rows, int cols, int frequency, int extractCount) {
    if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) {
        return;
    }
    if (!array || rows <= 0 || cols <= 0) {
        return;
    }

    // 计时：统计整体处理耗时
    QElapsedTimer timer;
    timer.start();

    // 1. 仅在滤波启用时执行并行滤波，避免“禁用滤波”时仍然创建线程任务。
    if (m_isFilterEnabled) {
        // 在行数、采样率或截止频率变化后重建滤波器，确保参数与当前配置一致。
        if (rows != m_lastRows || frequency != m_frequency || m_filterBankDirty) {
            createRowFilters(rows, frequency);
        }

        // 2. 并行处理滤波（直接操作原始数组）
        const int idealThreads = qMax(1, QThread::idealThreadCount());
        const int cpuBudget = qMax(1, idealThreads - 2); // 给 UI/音频线程预留核
        const int numThreads = qMax(1, qMin(qMin(4, cpuBudget), rows));
        const int blockSize = (rows + numThreads - 1) / numThreads; // 每个线程处理的行数
        QList<std::function<void()>> tasks;
        tasks.reserve(numThreads);

        for (int i = 0; i < numThreads; i++) {
            int start = i * blockSize;
            if (start >= rows) break;
            int end = qMin(rows, (i + 1) * blockSize);
            tasks.append(FilterWorker(array, cols, start, end,
                                      m_audioLocation, true, &m_rowFilters));
        }

        QFuture<void> future = QtConcurrent::map(tasks, [](std::function<void()> &task) {
            task();
        });
        future.waitForFinished();
    }

    // 3. 提取音频行数据：用于UI显示和FFT处理
    const int safeRows = qMax(1, rows); // 确保行数至少为1（防止越界）
    const int audioLen = qMax(0, qMin(cols, 5000)); // 音频数据长度（最多5000列，防止越界）
    QList<QVector<float>> multiAudioData; // 多音频行数据列表
    QVector<int> safeAudioRows;          // 安全的音频行索引（防止越界）
    multiAudioData.reserve(2); // 预分配2路音频数据内存
    safeAudioRows.reserve(2);

    // 提取2路音频行数据（防止行索引越界）
    for (int i = 0; i < 2; ++i) {
        // 限制行索引在[0, safeRows-1]范围内（安全检查）
        const int rowIndex = qBound(0, m_audioLocations.value(i, 0), safeRows - 1);
        safeAudioRows.append(rowIndex);
        // 计算当前音频行数据起始地址
        const float* rowData = array + rowIndex * cols;
        QVector<float> rowVec(audioLen); // 存储当前音频行数据
        // 拷贝数据到QVector（用于信号传递）
        if (audioLen > 0) {
            std::copy(rowData, rowData + audioLen, rowVec.begin());
        }
        multiAudioData.append(rowVec);
    }

   // 发送第一路音频数据信号到FFT模块
    if (!multiAudioData.isEmpty()) {
      emit array_tongguolvboSignal(multiAudioData.first(), audioLen, frequency);
    }
  //  发送多路音频数据信号（包含行索引）
  emit multiAudioRowsSignal(multiAudioData, safeAudioRows, audioLen, frequency, extractCount);

    // 更新截止频率参数
    m_frequency = frequency;

    // 4. 发送处理后的数据信号（区分滤波/非滤波状态）

    if(m_isFilterEnabled) {
        // 滤波启用：发送原始数组（已滤波）

    emit Filter_my_array_Signal(array, rows, cols, m_frequency, extractCount);
    } else {
        // 滤波禁用：列方向提取数据并转换为QList<QVector<float>>格式
        const int TARGET_COL_COUNT = 5;    // 目标提取列数（最终输出5列）
        const int TARGET_ROW_PER_COL = rows; // 每列包含的行数（与原始数组行数一致）
        extractedData.reserve(TARGET_COL_COUNT); // 预分配内存

        // 遍历目标列（5列），按均匀间隔提取原始数组的列数据
        for (int targetCol = 0; targetCol < TARGET_COL_COUNT; targetCol++) {
            QVector<float> colData; // 存储当前列的所有行数据
            colData.reserve(TARGET_ROW_PER_COL); // 预分配内存

            // 计算原始数组中当前目标列对应的列索引（均匀分布）
            int originalColStep = cols / TARGET_COL_COUNT; // 列提取步长
            int originalCol = targetCol * originalColStep; // 当前提取的原始列索引

            // 遍历所有行，提取当前列的所有行数据
            for (int row = 0; row < TARGET_ROW_PER_COL; row++) {
                // 计算原始数组中(row, originalCol)位置的指针
                float* dataPtr = array + row * cols + originalCol;
                colData.append(*dataPtr); // 将数据添加到列向量中
            }

            // 将当前列数据添加到最终提取结果中
            extractedData.append(colData);
        }

        // 发送时间提取后的数据信号
        emit Data_after_time_Extraction(extractedData, rows, cols, m_frequency, extractCount);
        // 清空提取数据（避免下一次处理时数据残留）
        extractedData.clear();
    }

    // 发送处理耗时信号（用于性能监控）
    emit processingTimeMeasured(timer.elapsed(), m_frequency);
}
