/**
 * @file unwrap.cpp
 * @brief 连续帧相位解缠、可选时间差分与保存快照分流。
 *
 * 对每个空间行沿列方向维护上一样本的解缠状态；根据相邻值跨越 ±pi 的次数累积 2pi 偏移，
 * 从而把折返相位还原为连续序列。尺寸或采集配置变化、滤波状态切换时均会清除记忆。
 * 保存支路从处理后矩阵复制 QByteArray 快照，保证后台写文件不依赖会被下一帧复用的工作缓冲。
 */
#include "unwrap.h"
#include "audio.h"
#include "ParameterManager.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QFuture>
#include <QDir>
#include <QDateTime>
#include <QDataStream>
#include <vector>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>
#include <limits>

namespace {
// 调试日志：将指定row整行数据按“单列”写入CSV（每个采样点占一行，持续追加）
/**
 * @brief 按当前保存策略写入或保留数据，并沿用既有的格式和错误处理。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void appendRowValuesCsv(const QString& fileName, const float* rowData, int cols)
{
    if (!rowData || cols <= 0) {
        return;
    }

    static QMutex s_logMutex;
    QMutexLocker locker(&s_logMutex);

    QFile logFile(fileName);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream out(&logFile);
    for (int c = 0; c < cols; ++c) {
        out << QString::number(rowData[c], 'f', 9);
        out << '\n';
    }
}
}

// 全局变量：用于二进制文件保存和线程安全控制
QFile m_binFile;                // 用于保存数据的二进制文件对象
QDataStream* m_binStream = nullptr; // 二进制数据输出流（用于文件写入）
QMutex fileMutex;               // 文件操作互斥锁（保证多线程下文件操作安全）
bool write_only_flag = false;   // 未使用的标志位（仅定义，暂无实际功能）
Audio audioProcessor;           // 音频处理器对象（预留用于音频相关处理）
bool saveSigleFlag = false;     // 单点数据保存使能标志
bool saveAllFlag = false;       // 新增：保存全部数据的使能标志
bool is_filter = true;          // 滤波功能总开关标志（true启用，false禁用）

// Unwrap类：实现相位解缠绕功能
// 核心功能：对二维浮点数组进行相位解缠绕（消除2π相位跳变）、数据转发、参数响应
/**
 * @brief 执行本阶段的数据计算与转换，并保持输入输出序列的既有约束。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
Unwrap::Unwrap(QObject *parent)
    : QObject(parent),
    isFile2Open(false),       // 文件打开状态标志（初始为未打开）
    m_array(nullptr),         // 指向待处理数据的指针（初始为空）
    m_rows(0),                // 数据数组的行数（初始为0）
    m_cols(0),                // 数据数组的列数（初始为0）
    m_frequency(0)
{
    // 连接参数管理器的参数变化信号：响应全局参数更新
    connect(&ParameterManager::instance(), &ParameterManager::parameterChanged, this, &Unwrap::onParameterChanged);

    m_diffPhaseEnabled = ParameterManager::instance().getParameter("enable_diff_phase").toBool();

    bool ok = false;
    const int row = ParameterManager::instance().getParameter("monitorPosition").toInt(&ok);
    if (ok && row >= 0) {
        m_snapshotRow = row;
    }
}

// 接收滤波使能状态的槽函数
// 参数：ok - true启用滤波，false禁用滤波
/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Unwrap::Receive_is_filter(bool ok) {
    is_filter = ok;
    if (ok != m_lastFilterEnabled) {
        resetUnwrapMemory();
        m_lastFilterEnabled = ok;
    }
}

// 参数变化响应槽函数：处理全局参数更新
// 参数：
// key - 变化的参数名
// value - 参数的新值
/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Unwrap::onParameterChanged(const QString& key, const QVariant& value) {
    // 监听"monitorPosition"（监听行位置）参数变化
    if (key == "monitorPosition") {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (ok && row >= 0) {
            m_snapshotRow = row;
        }
    } else if (key == "enable_diff_phase") {
        const bool enabled = value.toBool();
        if (m_diffPhaseEnabled != enabled) {
            m_diffPhaseEnabled = enabled;
            resetDiffPhaseMemory();
        }
    }
}

// 核心数据处理槽函数：接收原始数据并执行相位解缠绕，根据保存标志写入文件，最终转发处理后数据
// 参数：
// array - 待处理的浮点型数据数组指针（行优先存储）
// rows - 数据数组的行数
// cols - 数据数组的列数
// frequency - 当前数据对应的频率值
// extractCount - 数据提取批次计数（用于标记数据批次）
// start - 预留参数（数据起始位置）
// end - 预留参数（数据结束位置）
// differentialDistance - 预留参数（差分距离）
/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Unwrap::Receive_rebuild_data(float* array, int rows, int cols, int frequency, int extractCount, int start, int end, int differentialDistance) {
    if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) {
        return;
    }

    // 加互斥锁：保证多线程下数据操作安全（防止文件/数组同时被修改）
    // QMutexLocker locker(&fileMutex);

    // 分段/配置变化时，清理解缠绕记忆，避免段间跳变
    const bool streamConfigChanged =
        (rows != m_rows) ||
        (cols != m_cols) ||
        (extractCount != m_lastExtractCount) ||
        (start != m_lastStart) ||
        (end != m_lastEnd) ||
        (differentialDistance != m_lastDifferentialDistance);
    if (streamConfigChanged) {
        resetUnwrapMemory();
    }

    // 记录当前参数（frequency 必须透传到下游，避免滤波器使用到无效采样率）
    m_frequency = frequency;
    m_array = array;
    m_rows = rows;
    m_cols = cols;
    m_lastExtractCount = extractCount;
    m_lastStart = start;
    m_lastEnd = end;
    m_lastDifferentialDistance = differentialDistance;

    // 计时：统计解缠绕处理总耗时（用于性能监控）
    QElapsedTimer time;
    time.start();

    // 采样指定row整行数据（由 monitorPosition 指定）用于验证解缠绕前后差异
    const int fixedRow1Based = 100;
    const int targetRow = qBound(0, fixedRow1Based - 1, rows - 1); // 第100行=99

    // if (array && rows > 0 && cols > 0 && differentialDistance == 10) {
    //     const float* rowBefore = array + targetRow * cols;
    //     appendRowValuesCsv("E:/unwrap_row_before_values.csv", rowBefore, cols);
    // }

    array_unwrap_matrix_memory(array, rows, cols);
    if (m_diffPhaseEnabled) {
        applyDiffPhase(array, rows, cols);
    }

    // if (array && rows > 0 && cols > 0 && differentialDistance == 10) {
    //     const float* rowAfter = array + targetRow * cols;
    //     appendRowValuesCsv("E:/unwrap_row_after_values.csv", rowAfter, cols);
    // }

    const bool saveAllActive = ParameterManager::instance().getParameter("save_all_active").toBool();
    const bool saveSingleActive = ParameterManager::instance().getParameter("save_single_active").toBool();
    if (saveAllActive || saveSingleActive) {
        const qint64 rowBytes = static_cast<qint64>(cols) * static_cast<qint64>(sizeof(float));
        const qint64 totalBytes = static_cast<qint64>(rows) * rowBytes;
        if (saveAllActive && totalBytes > 0 && totalBytes <= std::numeric_limits<int>::max()) {
            QByteArray snapshot(reinterpret_cast<const char*>(array), static_cast<int>(totalBytes));
            emit Unwrap_Data_SaveSnapshotSignal(snapshot,
                                                true,
                                                -1,
                                                rows,
                                                cols,
                                                m_frequency,
                                                extractCount,
                                                start,
                                                end,
                                                differentialDistance);
        } else if (saveSingleActive && rowBytes > 0 && rowBytes <= std::numeric_limits<int>::max()) {
            const int requestedRow = ParameterManager::instance().getParameter("active_single_save_row").toInt();
            const int snapshotRow = qBound(0, requestedRow, rows - 1);
            const float* rowData = array + snapshotRow * cols;
            QByteArray snapshot(reinterpret_cast<const char*>(rowData), static_cast<int>(rowBytes));
            emit Unwrap_Data_SaveSnapshotSignal(snapshot,
                                                false,
                                                snapshotRow,
                                                rows,
                                                cols,
                                                m_frequency,
                                                extractCount,
                                                start,
                                                end,
                                                differentialDistance);
        }
    }

    // 发送处理后的数据信号：供UI显示、其他模块使用
    emit Unwrap_Data_Signal(array, rows, cols, m_frequency, extractCount);

    // 统计解缠绕耗时并发送耗时信号（性能监控）
    qint64 untime = time.elapsed();
    emit preserveProcessingTime(untime, m_frequency);
}

// 核心函数：对整个二维浮点数组的每一行执行带记忆的相位解缠绕（优化版）
// 功能：消除相位数据中的2π跳变，每行独立记忆上一次处理的最后值，保证相位连续
// 参数：
// array - 待处理的二维数组指针（行优先存储）
// rows - 数组行数
// cols - 数组列数
/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Unwrap::array_unwrap_matrix_memory(float* array, int rows, int cols) {
    if (!array || rows <= 0 || cols <= 0) {
        return;
    }

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
    static constexpr float INV_TWO_PI = 1.0f / TWO_PI;

    // 行数变化时重置记忆；复用已有容量，避免重复分配临时对象
    if (m_unwrap_row_memory.size() != rows) {
        m_unwrap_row_memory.resize(rows);
        m_unwrap_row_memory.fill(0.0f);
    }

    float* rowMemory = m_unwrap_row_memory.data();

    // 逐行处理：保持原有解缠逻辑（n = floor((diff + PI) / TWO_PI)）
    for (int row = 0; row < rows; ++row) {
        float* rowPtr = array + row * cols;

        float first = rowPtr[0];
        float firstDiff = first - rowMemory[row];
        if (!(firstDiff >= -PI && firstDiff < PI)) {
            const int n = static_cast<int>(std::floor((firstDiff + PI) * INV_TWO_PI));
            first -= static_cast<float>(n) * TWO_PI;
            rowPtr[0] = first;
        }

        float prev = rowPtr[0];
        float* p = rowPtr + 1;
        float* end = rowPtr + cols;
        for (; p != end; ++p) {
            float curr = *p;
            float diff = curr - prev;
            if (!(diff >= -PI && diff < PI)) {
                const int m = static_cast<int>(std::floor((diff + PI) * INV_TWO_PI));
                curr -= static_cast<float>(m) * TWO_PI;
                *p = curr;
            }
            prev = curr;
        }

        rowMemory[row] = prev;
    }
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Unwrap::applyDiffPhase(float* array, int rows, int cols)
{
    if (!array || rows <= 0 || cols <= 0) {
        return;
    }

    if (m_diff_phase_row_memory.size() != rows) {
        m_diff_phase_row_memory.resize(rows);
        m_diff_phase_row_memory.fill(0.0f);
        m_hasDiffPhaseHistory = false;
    }

    float* lastValues = m_diff_phase_row_memory.data();
    for (int row = 0; row < rows; ++row) {
        float* rowPtr = array + row * cols;
        float previousOriginal = rowPtr[0];
        const float previousFrameLast = lastValues[row];

        rowPtr[0] = m_hasDiffPhaseHistory ? (previousOriginal - previousFrameLast) : 0.0f;
        for (int col = 1; col < cols; ++col) {
            const float currentOriginal = rowPtr[col];
            rowPtr[col] = currentOriginal - previousOriginal;
            previousOriginal = currentOriginal;
        }

        lastValues[row] = previousOriginal;
    }

    m_hasDiffPhaseHistory = true;
}

/**
 * @brief 清理当前处理阶段的临时状态与持有资源，恢复可预测的后续运行条件。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Unwrap::resetUnwrapMemory()
{
    unwrap_data = 0.0f;
    m_unwrap_row_memory.fill(0.0f);
    resetDiffPhaseMemory();
}

/**
 * @brief 清理当前处理阶段的临时状态与持有资源，恢复可预测的后续运行条件。
 * @details 此实现属于相位展开与差分计算层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Unwrap::resetDiffPhaseMemory()
{
    m_diff_phase_row_memory.fill(0.0f);
    m_hasDiffPhaseHistory = false;
}

