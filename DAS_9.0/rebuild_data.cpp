/**
 * @file rebuild_data.cpp
 * @brief 将 FPGA 列主序 DMA 数据重构为行主序的空间差分相位矩阵。
 *
 * buildDiffFromRaw 校验空间裁剪范围，解码每个四字节原始样本的高 16 位相位，并计算相隔
 * differentialDistance 的差；随后按 extractCount 做空间抽取。列区间借助 QtConcurrent 并行处理。
 * mode 18/28 各有一对 ping-pong 输出矩阵，避免下游异步槽尚在读取时覆盖上一帧。
 */
#include "rebuild_data.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QVector>
#include <QtConcurrent>
#include <new>

#include "ParameterManager.h"

namespace {
constexpr int kMaxRows = 1024 * 24;
constexpr int kMaxCols = 5000;
constexpr int kBytesPerSample = 4;
constexpr float kPhaseScale = 1.0f / 8192.0f;
constexpr bool kEnableBuildDiffProfiling = true;
constexpr int kBuildDiffProfileEveryN = 20;
quint64 g_buildDiffProfileSeq = 0;

// Ping-pong buffers per mode:
// mode18: A/B, mode28: A/B (total 4 large buffers).
float (*g_output_mode18_buffers[2])[kMaxCols] = {
    new(std::nothrow) float[kMaxRows][kMaxCols],
    new(std::nothrow) float[kMaxRows][kMaxCols]
};
float (*g_output_mode28_buffers[2])[kMaxCols] = {
    new(std::nothrow) float[kMaxRows][kMaxCols],
    new(std::nothrow) float[kMaxRows][kMaxCols]
};
int g_mode18_emit_index = 0;
int g_mode28_emit_index = 0;

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于原始采样重组层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
inline float decodeRawPhaseSample(const unsigned char* src)
{
    // 原始 DMA 数据里每个采样点占 4 字节，这里只取高 16 位作为相位值。
    // 先按有符号 16 位整数解释，再乘缩放系数还原成浮点相位。
    const uint16_t highWord = static_cast<uint16_t>(src[2]) |
                              (static_cast<uint16_t>(src[3]) << 8);
    return static_cast<float>(static_cast<int16_t>(highWord)) * kPhaseScale;
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于原始采样重组层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
inline int buildDiffFromRaw(const unsigned char* src,
                            float (*dst)[kMaxCols],
                            int realRows,
                            int realCols,
                            int start,
                            int end,
                            int differentialDistance,
                            int extractCount)
{
    const quint64 profileSeq = ++g_buildDiffProfileSeq;
    const bool profileThisCall =
        kEnableBuildDiffProfiling &&
        (kBuildDiffProfileEveryN <= 1 || (profileSeq % static_cast<quint64>(kBuildDiffProfileEveryN) == 0));

    QElapsedTimer totalTimer;
    QElapsedTimer stageTimer;
    QString startStamp;
    qint64 validateMs = 0;
    qint64 cropMs = 0;
    qint64 diffMs = 0;

    if (profileThisCall) {
        startStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        totalTimer.start();
        stageTimer.start();
    }

    auto logProfile = [&](const char* reason, int outRows) {
        if (!profileThisCall) return;

        const qint64 totalMs = totalTimer.elapsed();
        qint64 maxMs = validateMs;
        QString maxStage = QStringLiteral("validate");
        if (cropMs > maxMs) {
            maxMs = cropMs;
            maxStage = QStringLiteral("crop");
        }
        if (diffMs > maxMs) {
            maxMs = diffMs;
            maxStage = QStringLiteral("diff");
        }

        qDebug().noquote()
            << QString("[Rebuild][buildDiffFromRaw] seq=%1 start=%2 reason=%3 rows=%4 cols=%5 extract=%6 diff=%7 outRows=%8 validateMs=%9 cropMs=%10 diffMs=%11 totalMs=%12 maxStage=%13(%14ms)")
                  .arg(profileSeq)
                  .arg(startStamp)
                  .arg(QString::fromLatin1(reason))
                  .arg(realRows)
                  .arg(realCols)
                  .arg(extractCount)
                  .arg(differentialDistance)
                  .arg(outRows)
                  .arg(validateMs)
                  .arg(cropMs)
                  .arg(diffMs)
                  .arg(totalMs)
                  .arg(maxStage)
                  .arg(maxMs);
    };

    if (!src || !dst || realRows <= 0 || realCols <= 0) {
        if (profileThisCall) {
            validateMs = stageTimer.elapsed();
        }
        logProfile("invalid_input", 0);
        return 0;
    }

    const int safeExtract = qMax(1, extractCount);
    const int safeDiff = qMax(1, differentialDistance);
    const int startRow = qBound(0, start, realRows - 1);
    const int endRow = qBound(startRow, end, realRows);
    if (profileThisCall) {
        validateMs = stageTimer.elapsed();
        stageTimer.restart();
    }

    const int lastBaseExclusive = qMin(endRow, realRows - safeDiff);
    if (startRow >= lastBaseExclusive) {
        if (profileThisCall) {
            cropMs = stageTimer.elapsed();
        }
        logProfile("empty_after_crop_bounds", 0);
        return 0;
    }

    const int outRows = 1 + (lastBaseExclusive - 1 - startRow) / safeExtract;

    if (profileThisCall) {
        cropMs = stageTimer.elapsed();
        stageTimer.restart();
    }

    const size_t colStrideBytes = static_cast<size_t>(realRows) * kBytesPerSample;
    const size_t diffStrideBytes = static_cast<size_t>(safeDiff) * kBytesPerSample;
    const size_t extractStrideBytes = static_cast<size_t>(safeExtract) * kBytesPerSample;
    const int idealThreads = qMax(1, QThread::idealThreadCount());
    const int configuredThreads = qMax(1, ParameterManager::instance().getParameter("numThreads").toInt());
    const int cpuBudget = qMax(1, idealThreads - 1);
    const int workerCount = qMax(1, qMin(qMin(configuredThreads, cpuBudget), realCols));
    const int colsPerTask = qMax(1, (realCols + workerCount - 1) / workerCount);

    QVector<int> taskIds;
    taskIds.reserve(workerCount);
    for (int taskIndex = 0; taskIndex < workerCount; ++taskIndex) {
        if (taskIndex * colsPerTask >= realCols) break;
        taskIds.push_back(taskIndex);
    }

    auto processTask = [&](int taskIndex) {
        const int startCol = taskIndex * colsPerTask;
        const int endCol = qMin(realCols, startCol + colsPerTask);

        for (int col = startCol; col < endCol; ++col) {
            const unsigned char* srcCol = src + static_cast<size_t>(col) * colStrideBytes;
            size_t baseOffsetBytes = static_cast<size_t>(startRow) * kBytesPerSample;
            size_t diffOffsetBytes = baseOffsetBytes + diffStrideBytes;
            for (int outRow = 0; outRow < outRows; ++outRow) {
                dst[outRow][col] = decodeRawPhaseSample(srcCol + diffOffsetBytes) -
                                   decodeRawPhaseSample(srcCol + baseOffsetBytes);
                baseOffsetBytes += extractStrideBytes;
                diffOffsetBytes += extractStrideBytes;
            }
        }
    };

    if (taskIds.size() <= 1) {
        if (!taskIds.isEmpty()) {
            processTask(taskIds.first());
        }
    } else {
        QFuture<void> future = QtConcurrent::map(taskIds, [&](int &taskIndex) {
            processTask(taskIndex);
        });
        future.waitForFinished();
    }

    if (profileThisCall) {
        diffMs = stageTimer.elapsed();
    }

    logProfile("ok", outRows);
    return outRows;
}
} // namespace

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于原始采样重组层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
Rebuild_data::Rebuild_data(MainWindow* parent)
{
    Q_UNUSED(parent);
}

/**
 * @brief 按当前保存策略写入或保留数据，并沿用既有的格式和错误处理。
 * @details 此实现属于原始采样重组层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Rebuild_data::saveRowsToCSV(float (*array)[5000], int rows, int cols, int arrayIndex)
{
    Q_UNUSED(array);
    Q_UNUSED(rows);
    Q_UNUSED(cols);
    Q_UNUSED(arrayIndex);
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于原始采样重组层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Rebuild_data::Receive_raw_data(unsigned char* rawData,
                                    int mode,
                                    int real_rows,
                                    int real_cols,
                                    int real_frequency,
                                    int extractCount,
                                    int start,
                                    int end,
                                    int differentialDistance)
{
    if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) {
        return;
    }
    if (!rawData || real_rows <= 0 || real_cols <= 0) return;
    if (real_rows > kMaxRows || real_cols > kMaxCols) return;
    if (!g_output_mode18_buffers[0] || !g_output_mode18_buffers[1] ||
        !g_output_mode28_buffers[0] || !g_output_mode28_buffers[1]) {
        return;
    }

    float (*target)[kMaxCols] = nullptr;
    int* emitIndex = nullptr;
    if (mode == 18) {
        target = g_output_mode18_buffers[g_mode18_emit_index];
        emitIndex = &g_mode18_emit_index;
    } else if (mode == 28) {
        target = g_output_mode28_buffers[g_mode28_emit_index];
        emitIndex = &g_mode28_emit_index;
    } else {
        return;
    }

    const int safeExtract = qMax(1, extractCount);
    const int startChannel = qMax(0, start);
    const int endChannel = qMax(startChannel, end);
    const int startRaw = startChannel * safeExtract;
    const int endRaw = endChannel * safeExtract;

    QElapsedTimer timer;
    timer.start();
    const int outputRows = buildDiffFromRaw(rawData,
                                            target,
                                            real_rows,
                                            real_cols,
                                            startRaw,
                                            endRaw,
                                            differentialDistance,
                                            extractCount);
    const qint64 rebuildMs = timer.elapsed();

    qint64 downstreamWaitMs = 0;
    if (outputRows > 0) {
        emit widget_my_array_Signal(*target,
                                    outputRows,
                                    real_cols,
                                    real_frequency,
                                    extractCount,
                                    startChannel,
                                    endChannel,
                                    differentialDistance);
        downstreamWaitMs = timer.elapsed() - rebuildMs;
    }

    if (emitIndex) {
        *emitIndex ^= 1;
    }

    // UI“数据重构”显示纯重构算法耗时（不包含下游阻塞等待）。
    emit totalProcessingTime(rebuildMs, real_frequency);

    static quint64 s_rebuildStageSeq = 0;
    ++s_rebuildStageSeq;
    if ((s_rebuildStageSeq % 20u) == 0u || downstreamWaitMs > 20) {
        qDebug().noquote()
            << QString("[Rebuild][stage] seq=%1 mode=%2 rebuildMs=%3 downstreamWaitMs=%4 outputRows=%5")
                  .arg(s_rebuildStageSeq)
                  .arg(mode)
                  .arg(rebuildMs)
                  .arg(downstreamWaitMs)
                  .arg(outputRows);
    }
}
