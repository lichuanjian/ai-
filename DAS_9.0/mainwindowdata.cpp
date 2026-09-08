/**
 * @file mainwindowdata.cpp
 * @brief 实时数据到 QCustomPlot 的节流渲染、缓存与显示模式切换。
 *
 * 音频数据先进入每通道队列，再由定时器按预算消耗，避免高速信号直接触发大量 replot；RMS/应变保留最新帧，
 * 可在瀑布图、定位曲线、距离横轴和通道横轴间重绘。FFT 同样支持频谱瀑布和单帧曲线两种显示路径。
 */
#include "MainWindowData.h"
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <iterator>
#include  "mainwindowplot.h"
#include "mainwindowutils.h"
#include "ParameterManager.h"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kAudioMaxPoints = 3000000;
constexpr double kAudioMaxHistorySeconds = 3.0 * 60.0 * 60.0;
constexpr double kAudioMinVisibleSeconds = 5.0;
constexpr double kAudioFollowEpsilon = 0.25;
constexpr qint64 kAudioRenderIntervalMs = 20;
constexpr int kAudioRenderMinTargetPoints = 32;
constexpr int kAudioRenderMaxTargetPoints = 30000;
constexpr double kAudioRenderPointsPerPixel = 8.0;
constexpr double kAudioAutoScaleFactor = 1.6;
constexpr double kAudioAutoScaleMinAbs = 0.1;
constexpr int kWaterfallVisibleRows = 100;
constexpr qint64 kWaterfallReplotMinIntervalMs = 33;
constexpr qint64 kFftReplotMinIntervalMs = 33;

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
QCPRange defaultRmsLineRange(bool strainMode)
{
    return strainMode ? QCPRange(-50.0, 50.0) : QCPRange(0.0, 6.0);
}

template <typename Transform>
/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void insertWaterfallBatch(QCPColorMapData *mapData,
                          const QList<QVector<float>> &frames,
                          int mapCols,
                          int visibleRows,
                          Transform transform,
                          bool clearBeforeInsert,
                          bool zeroFillMissingCols)
{
    if (!mapData || frames.isEmpty() || mapCols <= 0 || visibleRows <= 0) return;

    const int batchRows = qMin(visibleRows, frames.size());
    const int frameStart = frames.size() - batchRows;

    if (clearBeforeInsert) {
        mapData->fill(0.0);
    } else if (batchRows < visibleRows) {
        for (int y = visibleRows - 1; y >= batchRows; --y) {
            for (int x = 0; x < mapCols; ++x) {
                mapData->setCell(x, y, mapData->cell(x, y - batchRows));
            }
        }
    }

    for (int rowOffset = 0; rowOffset < batchRows; ++rowOffset) {
        const QVector<float> &frame = frames[frameStart + batchRows - 1 - rowOffset];
        const int drawCols = qMin(mapCols, frame.size());
        for (int x = 0; x < drawCols; ++x) {
            mapData->setCell(x, rowOffset, transform(frame[x]));
        }
        if (zeroFillMissingCols && drawCols < mapCols) {
            for (int x = drawCols; x < mapCols; ++x) {
                mapData->setCell(x, rowOffset, 0.0);
            }
        }
    }
}
} // namespace

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
MainWindowData::MainWindowData(QObject *parent) : QObject(parent)
{
    m_audioRenderTimer = new QTimer(this);
    m_audioRenderTimer->setTimerType(Qt::PreciseTimer);
    m_audioRenderTimer->setInterval(static_cast<int>(kAudioRenderIntervalMs));
    connect(m_audioRenderTimer, &QTimer::timeout, this, &MainWindowData::onAudioRenderTick);
}

/**
 * @brief 清空或复位当前功能相关的状态，确保后续流程从一致状态继续。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::shutdown()
{
    m_shuttingDown = true;
    if (m_audioRenderTimer) {
        m_audioRenderTimer->stop();
    }
    for (std::deque<float> &buffer : m_audioPendingBuffers) {
        buffer.clear();
    }
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::setAudioPlot(QCustomPlot *plot)
{
    m_audioPlot = plot;
    m_audioPlots.clear();
    m_audioTimeKeys.clear();
    if (plot) {
        m_audioPlots.append(plot);
        m_audioTimeKeys.append(0.0);
    }
    ensureAudioPendingBufferCount();
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::setAudioPlots(const QVector<QCustomPlot*>& plots)
{
    m_audioPlots = plots;
    m_audioPlot = m_audioPlots.isEmpty() ? nullptr : m_audioPlots.first();
    m_audioTimeKeys = QVector<double>(m_audioPlots.size(), 0.0);
    ensureAudioPendingBufferCount();
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::setAudioYAxisAutoScale(int channelIndex, bool enabled)
{
    if (channelIndex < 0 || channelIndex >= m_audioPlots.size()) return;
    ensureAudioPendingBufferCount();
    m_audioAutoScaleEnabled[channelIndex] = enabled;
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::setRMSWaterPlot(QCustomPlot *plot)
{
    m_rmsWaterPlot = plot;
    m_forceRmsAxisRefresh = true;
}
/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::setFFTWaterPlot(QCustomPlot *plot)
{
    if (m_fftXAxisRangeConn) {
        QObject::disconnect(m_fftXAxisRangeConn);
    }
    if (m_fftYAxisRangeConn) {
        QObject::disconnect(m_fftYAxisRangeConn);
    }

    m_fftWaterPlot = plot;
    m_fftAxisLockedByUser = false;
    m_fftApplyingAxisUpdate = false;

    if (!m_fftWaterPlot || !m_fftWaterPlot->xAxis || !m_fftWaterPlot->yAxis) return;
    const auto axisRangeChanged = static_cast<void (QCPAxis::*)(const QCPRange&)>(&QCPAxis::rangeChanged);

    m_fftXAxisRangeConn = QObject::connect(m_fftWaterPlot->xAxis,
                                           axisRangeChanged,
                                           this,
                                           [this](const QCPRange&) {
                                               if (!m_fftApplyingAxisUpdate) {
                                                   m_fftAxisLockedByUser = true;
                                               }
                                           });
    m_fftYAxisRangeConn = QObject::connect(m_fftWaterPlot->yAxis,
                                           axisRangeChanged,
                                           this,
                                           [this](const QCPRange&) {
                                               if (!m_fftApplyingAxisUpdate) {
                                                   m_fftAxisLockedByUser = true;
                                               }
                                           });
}

/**
 * @brief 清空或复位当前功能相关的状态，确保后续流程从一致状态继续。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::resetAudioTimeline()
{
    if (m_shuttingDown) return;
    m_audioTimeKey = 0.0;
    m_audioTimeKeys = QVector<double>(m_audioPlots.size(), 0.0);
    ensureAudioPendingBufferCount();
    for (std::deque<float> &buffer : m_audioPendingBuffers) {
        buffer.clear();
    }
    if (m_audioRenderTimer) {
        m_audioRenderTimer->stop();
    }
}

/**
 * @brief 清空或复位当前功能相关的状态，确保后续流程从一致状态继续。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::clearAudioChannelData(int channelIndex)
{
    if (m_shuttingDown) return;
    if (channelIndex < 0 || channelIndex >= m_audioPlots.size()) return;

    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, channelIndex]() {
            clearAudioChannelData(channelIndex);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_audioTimeKeys.size() != m_audioPlots.size()) {
        m_audioTimeKeys = QVector<double>(m_audioPlots.size(), 0.0);
    }
    ensureAudioPendingBufferCount();
    m_audioTimeKeys[channelIndex] = 0.0;
    if (channelIndex < m_audioPendingBuffers.size()) {
        m_audioPendingBuffers[channelIndex].clear();
    }

    QCustomPlot *plot = m_audioPlots[channelIndex];
    if (!plot) return;

    if (plot->graphCount() > 0 && plot->graph(0) && plot->graph(0)->data()) {
        plot->graph(0)->data()->clear();
    }
    if (plot->graphCount() > 1 && plot->graph(1) && plot->graph(1)->data()) {
        plot->graph(1)->data()->clear();
    }
    plot->xAxis->setRange(0, kAudioMinVisibleSeconds);
    plot->yAxis->setRange(m_strainValueMode ? QCPRange(-50.0, 50.0) : QCPRange(-0.1, 0.1));
    plot->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::ensureAudioPendingBufferCount()
{
    const int plotCount = m_audioPlots.size();
    const int oldAutoScaleSize = m_audioAutoScaleEnabled.size();

    m_audioPendingBuffers.resize(plotCount);
    m_audioAutoScaleEnabled.resize(plotCount);

    for (int i = oldAutoScaleSize; i < plotCount; ++i) {
        m_audioAutoScaleEnabled[i] = true;
    }
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
bool MainWindowData::shouldReplot(QElapsedTimer& timer, qint64 minIntervalMs)
{
    if (!timer.isValid()) {
        timer.start();
        return true;
    }
    if (timer.elapsed() >= minIntervalMs) {
        timer.restart();
        return true;
    }
    return false;
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
double MainWindowData::getPhaseToMicroStrainScale() const
{
    const double diffDistance = ParameterManager::instance().getParameter("differential_distance").toDouble();
    const int freq = qMax(1, m_rmsFrequency);
    const double rawPointMeter = MainWindowUtils::meterPerRawPoint(freq);
    const double gaugeLengthMeters = qMax(1e-6, diffDistance * rawPointMeter);
    const double denominator = 4.0 * kPi * m_groupIndex * gaugeLengthMeters * m_photoElasticFactor;
    if (denominator <= 0.0) return 0.0;
    return (m_opticalWavelength * 1e6) / denominator; // rad -> microstrain
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
double MainWindowData::transformAudioValue(float rawValue) const
{
    const double displayScale = m_strainValueMode ? getPhaseToMicroStrainScale() : 1.0;
    return static_cast<double>(rawValue) * displayScale;
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
bool MainWindowData::findAudioVisibleYRange(QCustomPlot* plot, double *minValue, double *maxValue) const
{
    if (!plot || plot->graphCount() < 2 || !plot->graph(1) || !plot->graph(1)->data()) return false;

    auto data = plot->graph(1)->data();
    if (!data || data->isEmpty()) return false;

    const QCPRange visibleX = plot->xAxis->range();
    auto beginIt = data->findBegin(visibleX.lower, true);
    auto endIt = data->findEnd(visibleX.upper, true);
    if (beginIt == endIt) {
        beginIt = data->constBegin();
        endIt = data->constEnd();
        if (beginIt == endIt) return false;
    }

    double minVal = beginIt->value;
    double maxVal = beginIt->value;
    for (auto it = beginIt; it != endIt; ++it) {
        if (it->value < minVal) minVal = it->value;
        if (it->value > maxVal) maxVal = it->value;
    }

    if (minValue) *minValue = minVal;
    if (maxValue) *maxValue = maxVal;
    return true;
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
QVector<double> MainWindowData::buildRmsXAxis(int pointCount, int frequency, int extractCount) const
{
    QVector<double> keys(qMax(0, pointCount));
    if (keys.isEmpty()) return keys;

    if (m_rmsXAxisChannelMode) {
        for (int i = 0; i < keys.size(); ++i) {
            keys[i] = static_cast<double>(i);
        }
        return keys;
    }

    const double meterPerChannel = MainWindowUtils::meterPerExtractedChannel(frequency, extractCount);
    for (int i = 0; i < keys.size(); ++i) {
        keys[i] = i * meterPerChannel;
    }
    return keys;
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::renderRmsLineFrame(const QVector<float>& frame,
                                        bool strainFrame,
                                        int rows,
                                        int frequency,
                                        int extractCount)
{
    if (!m_rmsWaterPlot || frame.isEmpty()) return;
    if (m_rmsWaterPlot->graphCount() <= 0 || !m_rmsWaterPlot->graph(0)) return;

    const int pointCount = qMin(qMax(1, rows), frame.size());
    if (pointCount <= 0) return;

    QVector<double> keys = buildRmsXAxis(pointCount, frequency, extractCount);
    QVector<double> values(pointCount);
    const double strainScale = strainFrame ? getPhaseToMicroStrainScale() : 1.0;

    double minVal = 0.0;
    double maxVal = 0.0;
    for (int i = 0; i < pointCount; ++i) {
        const double value = static_cast<double>(frame[i]) * strainScale;
        values[i] = value;
        if (i == 0 || value < minVal) minVal = value;
        if (i == 0 || value > maxVal) maxVal = value;
    }

    m_rmsWaterPlot->graph(0)->setData(keys, values, true);

    if (m_forceRmsAxisRefresh) {
        const double maxX = (keys.size() > 1)
                                ? keys.last()
                                : (m_rmsXAxisChannelMode
                                       ? 1.0
                                       : MainWindowUtils::meterPerExtractedChannel(frequency, extractCount));
        m_rmsWaterPlot->xAxis->setLabel(m_rmsXAxisChannelMode ? QStringLiteral("通道") : QStringLiteral("位置(m)"));
        m_rmsWaterPlot->xAxis->setRange(0, qMax(1.0, maxX));
        m_forceRmsAxisRefresh = false;
    }

    if (qFuzzyCompare(minVal, maxVal)) {
        const double span = strainFrame ? qMax(1.0, qAbs(minVal) * 0.2) : qMax(0.2, qAbs(minVal) * 0.2);
        minVal -= span;
        maxVal += span;
    }

    const QCPRange fallbackRange = defaultRmsLineRange(strainFrame);
    double lower = minVal;
    double upper = maxVal;
    if (!std::isfinite(lower) || !std::isfinite(upper)) {
        lower = fallbackRange.lower;
        upper = fallbackRange.upper;
    } else {
        const double pad = qMax(strainFrame ? 0.8 : 0.15, (upper - lower) * 0.18);
        lower -= pad;
        upper += pad;
    }
    if (lower >= upper) {
        lower = fallbackRange.lower;
        upper = fallbackRange.upper;
    }
    m_rmsWaterPlot->yAxis->setRange(lower, upper);

    if (shouldReplot(m_rmsReplotLimiter, kWaterfallReplotMinIntervalMs)) {
        m_rmsWaterPlot->replot(QCustomPlot::rpQueuedReplot);
    }
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::refreshRmsPlotFromCache()
{
    if (!m_rmsLineMode || !m_hasLatestRmsFrame || !m_rmsWaterPlot) return;
    if (m_latestRmsFrameIsStrain != m_strainValueMode) return;

    renderRmsLineFrame(m_latestRmsFrame,
                       m_latestRmsFrameIsStrain,
                       qMax(1, m_rmsRows),
                       qMax(1, m_rmsFrequency),
                       qMax(1, m_rmsExtractCount));
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::updateAudioYAxisToVisibleData(QCustomPlot* plot, bool forceUpdate)
{
    if (!plot || plot->graphCount() < 2 || !plot->graph(1) || !plot->graph(1)->data()) return;

    double minVal = 0.0;
    double maxVal = 0.0;
    if (!findAudioVisibleYRange(plot, &minVal, &maxVal)) {
        if (forceUpdate) {
            plot->yAxis->setRange(-1.0, 1.0);
        }
        return;
    }

    const double maxAbs = qMax(qAbs(minVal), qAbs(maxVal));
    const double limit = qMax(kAudioAutoScaleMinAbs, maxAbs * kAudioAutoScaleFactor);
    const QCPRange targetRange(-limit, limit);
    const QCPRange currentRange = plot->yAxis->range();

    if (forceUpdate ||
        qAbs(currentRange.lower - targetRange.lower) > 1e-6 ||
        qAbs(currentRange.upper - targetRange.upper) > 1e-6) {
        plot->yAxis->setRange(targetRange);
    }
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindowData::computeAudioRenderBudget(const std::deque<float>& pendingBuffer, int sampleRate) const
{
    const int safeRate = qMax(1, sampleRate);
    const int baseBudget = qMax(64, safeRate / 50);
    const int backlogBudget = static_cast<int>(pendingBuffer.size() / 3);
    const int maxBudget = qMax(baseBudget, safeRate / 5);
    return qMin(qMax(baseBudget, backlogBudget), qMax(baseBudget, maxBudget));
}



/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::renderAudioToPlot(int channelIndex, QCustomPlot* plot, double& timeKey, const QVector<float>& xdata, int len, int frequency)
{
    if (!plot || xdata.isEmpty()) return;
    if (QThread::currentThread() != plot->thread()) return;
    const int safeLen = qMax(0, qMin(len, xdata.size()));
    if (safeLen <= 0) return;
    if (plot->graphCount() < 2 || !plot->graph(1)) return;

    if (!std::isfinite(timeKey)) timeKey = 0.0;

    const QCPRange currentX = plot->xAxis->range();
    const double prevTail = timeKey;
    const bool followTail =
        (prevTail <= currentX.upper + kAudioFollowEpsilon) ||
        (currentX.upper >= prevTail - qMax(0.25, currentX.size() * 0.12));

    // Sample rate used for x-axis spacing.
    const double sampleRate = qMax(1, frequency);
    const double timeStep = 1.0 / sampleRate;

    const int plotWidth = qMax(1, plot->viewport().width());
    const int viewportBudgetPoints =
        qBound(kAudioRenderMinTargetPoints,
               static_cast<int>(plotWidth * kAudioRenderPointsPerPixel),
               kAudioRenderMaxTargetPoints);
    const int targetPointCount =
        qBound(kAudioRenderMinTargetPoints,
               viewportBudgetPoints,
               kAudioRenderMaxTargetPoints);

    QVector<double> vKeys;
    QVector<double> vValues;
    if (safeLen <= targetPointCount) {
        vKeys.resize(safeLen);
        vValues.resize(safeLen);
        for (int i = 0; i < safeLen; ++i) {
            vKeys[i] = timeKey + i * timeStep;
            vValues[i] = transformAudioValue(xdata[i]);
        }
    } else {
        const int drawCount = qBound(2, targetPointCount, safeLen);
        vKeys.resize(drawCount);
        vValues.resize(drawCount);
        const double step = static_cast<double>(safeLen - 1) / static_cast<double>(drawCount - 1);
        for (int i = 0; i < drawCount; ++i) {
            const int srcIndex = qBound(0, static_cast<int>(std::round(i * step)), safeLen - 1);
            vKeys[i] = timeKey + srcIndex * timeStep;
            vValues[i] = transformAudioValue(xdata[srcIndex]);
        }
    }

    // 时间轴按原始采样点推进，保持与采样流一致。
    timeKey += safeLen * timeStep;

    // Add samples to the "new data" graph.
    QCPGraph* graph = plot->graph(1);
    graph->addData(vKeys, vValues, true);
    if (graph->data()) {
        if (timeKey > kAudioMaxHistorySeconds) {
            graph->data()->removeBefore(timeKey - kAudioMaxHistorySeconds);
        }
        const int currentSize = graph->data()->size();
        if (currentSize > kAudioMaxPoints) {
            const int removeCount = currentSize - kAudioMaxPoints;
            auto it = graph->data()->constBegin();
            std::advance(it, removeCount);
            graph->data()->removeBefore(it->key);
        }
    }

    // 用户在查看历史区间时，不强制把视图拉回最新。
    if (followTail && timeKey > currentX.upper)
    {
        const double span = qMax(kAudioMinVisibleSeconds, currentX.size());
        plot->xAxis->setRange(timeKey, span, Qt::AlignRight);
    }

    const bool autoScaleY = m_audioAutoScaleEnabled.value(channelIndex, true);
    if (followTail && autoScaleY) {
        updateAudioYAxisToVisibleData(plot, timeKey < 1.0);
    }

    // 重绘由 onReceiveMultiAudioData 统一节流触发，避免每通道每帧都replot。
}

// Render audio data directly.
// void MainWindowData::onReceiveAudioData(const QVector<float>& xdata, int len)
// {
//     // qDebug() << "Cannot";
//     // if (m_audioPlots.isEmpty()) return;
//     // renderAudioToPlot(m_audioPlots.first(), m_audioTimeKeys[0], xdata, len, m_rmsFrequency);
// }

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::onReceiveMultiAudioData(const QList<QVector<float>>& rowsData, int len, int frequency)
{
    if (m_shuttingDown) return;
    if (rowsData.isEmpty() || m_audioPlots.isEmpty()) return;

    if (QThread::currentThread() != thread()) {
        const QList<QVector<float>> rowsCopy = rowsData;
        QMetaObject::invokeMethod(this, [this, rowsCopy, len, frequency]() {
            onReceiveMultiAudioData(rowsCopy, len, frequency);
        }, Qt::QueuedConnection);
        return;
    }

    const int count = qMin(rowsData.size(), m_audioPlots.size());
    if (m_audioTimeKeys.size() != m_audioPlots.size()) {
        m_audioTimeKeys = QVector<double>(m_audioPlots.size(), 0.0);
    }
    ensureAudioPendingBufferCount();
    m_audioPendingSampleRate = qMax(1, frequency);

    for (int i = 0; i < count; ++i) {
        const int rowLen = qMax(0, qMin(len, rowsData[i].size()));
        if (rowLen <= 0 || i >= m_audioPendingBuffers.size()) continue;

        std::deque<float> &pending = m_audioPendingBuffers[i];
        const QVector<float> &row = rowsData[i];
        for (int j = 0; j < rowLen; ++j) {
            pending.push_back(row[j]);
        }

        const int maxPendingSamples = qMax(m_audioPendingSampleRate, m_audioPendingSampleRate / 2 + 20000);
        if (static_cast<int>(pending.size()) > maxPendingSamples) {
            const int dropCount = static_cast<int>(pending.size()) - maxPendingSamples;
            for (int n = 0; n < dropCount; ++n) {
                pending.pop_front();
            }
        }
    }

    if (m_audioRenderTimer && !m_audioRenderTimer->isActive()) {
        m_audioRenderTimer->start();
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::onAudioRenderTick()
{
    if (m_shuttingDown) {
        if (m_audioRenderTimer) m_audioRenderTimer->stop();
        return;
    }
    if (m_audioPlots.isEmpty()) {
        if (m_audioRenderTimer) m_audioRenderTimer->stop();
        return;
    }

    ensureAudioPendingBufferCount();
    if (m_audioTimeKeys.size() != m_audioPlots.size()) {
        m_audioTimeKeys = QVector<double>(m_audioPlots.size(), 0.0);
    }

    bool renderedAny = false;
    bool hasPending = false;
    const int count = qMin(m_audioPlots.size(), m_audioPendingBuffers.size());

    for (int i = 0; i < count; ++i) {
        QCustomPlot *plot = m_audioPlots[i];
        if (!plot) continue;

        std::deque<float> &pending = m_audioPendingBuffers[i];
        if (pending.empty()) {
            continue;
        }
        hasPending = true;

        const int budget = computeAudioRenderBudget(pending, m_audioPendingSampleRate);
        const int takeCount = qMin(static_cast<int>(pending.size()), budget);
        if (takeCount <= 0) {
            continue;
        }

        QVector<float> chunk;
        chunk.resize(takeCount);
        for (int j = 0; j < takeCount; ++j) {
            chunk[j] = pending.front();
            pending.pop_front();
        }

        renderAudioToPlot(i, plot, m_audioTimeKeys[i], chunk, takeCount, m_audioPendingSampleRate);
        renderedAny = true;
        if (!pending.empty()) {
            hasPending = true;
        }
    }

    if (renderedAny) {
        for (int i = 0; i < count; ++i) {
            if (m_audioPlots[i]) {
                m_audioPlots[i]->replot(QCustomPlot::rpQueuedReplot);
            }
        }
    }

    if (!hasPending && m_audioRenderTimer) {
        m_audioRenderTimer->stop();
    }
}

// 鏍稿績淇敼锛歊MS 鏁版嵁鎺ユ敹鍒板悗鐩存帴娓叉煋锛屾棤闃熷垪銆佹棤瀹氭椂鍣?
/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::onReceiveRMSData(const QList<QVector<float>>& data, int rows, int cols, int frequency, int extractCount)
{
    if (m_shuttingDown) return;
    Q_UNUSED(cols);
    if (!m_rmsWaterPlot || data.isEmpty() || rows <= 0 || cols <= 0) return;

    const bool configChanged = (m_rmsRows != rows || m_rmsFrequency != frequency || m_rmsExtractCount != extractCount);
    m_latestRmsFrame = data.constLast();
    m_hasLatestRmsFrame = !m_latestRmsFrame.isEmpty();
    m_latestRmsFrameIsStrain = false;
    m_isStrainMode = false;
    m_rmsRows = rows;
    m_rmsFrequency = frequency;
    m_rmsExtractCount = extractCount;

    if (m_rmsPaused) return;
    if (m_rmsLineMode) {
        renderRmsLineFrame(m_latestRmsFrame, false, rows, frequency, extractCount);
        return;
    }

    if (m_rmsWaterPlot->plottableCount() <= 0) return;
    QCPAbstractPlottable *plottable = m_rmsWaterPlot->plottable(0);
    QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(plottable);
    if (!colorMap) return;

    QCPColorMapData *mapData = colorMap->data();
    if (!mapData) return;

    const bool axisModeChanged = m_forceRmsAxisRefresh;
    if (configChanged || axisModeChanged) {
        const double maxX = m_rmsXAxisChannelMode
                                ? static_cast<double>(qMax(1, m_rmsRows))
                                : static_cast<double>(MainWindowUtils::calculateMaxX(m_rmsRows, m_rmsExtractCount, m_rmsFrequency));
        m_rmsWaterPlot->xAxis->setLabel(m_rmsXAxisChannelMode ? QStringLiteral("通道") : QStringLiteral("位置(m)"));
        m_rmsWaterPlot->xAxis->setRange(0, maxX);
        m_rmsWaterPlot->yAxis->setRange(0, 100);
        mapData->setSize(m_rmsRows, 100);
        mapData->setRange(QCPRange(0, maxX), QCPRange(0, 100));
        m_forceRmsAxisRefresh = false;
    }

    const int mapCols = mapData->keySize();
    const int mapRows = mapData->valueSize();
    if (mapCols <= 0 || mapRows <= 0) return;

    const int drawRows = qMin(kWaterfallVisibleRows, mapRows);
    insertWaterfallBatch(mapData,
                         data,
                         mapCols,
                         drawRows,
                         [](float value) { return static_cast<double>(value); },
                         false,
                         true);

    if (shouldReplot(m_rmsReplotLimiter, kWaterfallReplotMinIntervalMs)) {
        m_rmsWaterPlot->replot(QCustomPlot::rpQueuedReplot);
    }
}




/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::onReceiveStrainData(const QList<QVector<float>>& data, int rows, int cols, int frequency, int extractCount)
{
    if (m_shuttingDown) return;
    Q_UNUSED(cols);
    if (!m_rmsWaterPlot || data.isEmpty() || rows <= 0 || cols <= 0) return;

    const bool configChanged = (m_rmsRows != rows || m_rmsFrequency != frequency || m_rmsExtractCount != extractCount);
    m_latestRmsFrame = data.constLast();
    m_hasLatestRmsFrame = !m_latestRmsFrame.isEmpty();
    m_latestRmsFrameIsStrain = true;
    m_rmsRows = rows;
    m_rmsFrequency = frequency;
    m_rmsExtractCount = extractCount;

    if (m_rmsPaused) return;
    if (m_rmsLineMode) {
        renderRmsLineFrame(m_latestRmsFrame, true, rows, frequency, extractCount);
        return;
    }

    if (m_rmsWaterPlot->plottableCount() <= 0) return;
    QCPAbstractPlottable *plottable = m_rmsWaterPlot->plottable(0);
    QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(plottable);
    if (!colorMap)
    {
        qWarning() << "Failed to get QCPColorMap from RMS plot!";
        return;
    }

    QCPColorMapData *mapData = colorMap->data();
    if (!mapData)
    {
        qWarning() << "QCPColorMap data is null!";
        return;
    }

    const bool axisModeChanged = m_forceRmsAxisRefresh;

    if (configChanged || axisModeChanged) {
        const double maxX = m_rmsXAxisChannelMode
                                ? static_cast<double>(qMax(1, m_rmsRows))
                                : static_cast<double>(MainWindowUtils::calculateMaxX(m_rmsRows, m_rmsExtractCount, m_rmsFrequency));
        m_rmsWaterPlot->xAxis->setLabel(m_rmsXAxisChannelMode ? QStringLiteral("通道") : QStringLiteral("位置(m)"));
        m_rmsWaterPlot->xAxis->setRange(0, maxX);
        m_rmsWaterPlot->yAxis->setRange(0, 100);
        mapData->setSize(m_rmsRows, 100);
        mapData->setRange(QCPRange(0, maxX), QCPRange(0, 100));
        m_forceRmsAxisRefresh = false;
    }

    const int mapCols = mapData->keySize();
    const int mapRows = mapData->valueSize();
    if (mapCols <= 0 || mapRows <= 0) return;

    const int COLS = mapCols;
    const int ROWS = qMin(kWaterfallVisibleRows, mapRows);

    const bool clearForModeSwitch = !m_isStrainMode;
    const double strainScale = getPhaseToMicroStrainScale();
    insertWaterfallBatch(mapData,
                         data,
                         COLS,
                         ROWS,
                         [strainScale](float value) { return static_cast<double>(value) * strainScale; },
                         clearForModeSwitch,
                         true);
    m_isStrainMode = true;

    if (shouldReplot(m_rmsReplotLimiter, kWaterfallReplotMinIntervalMs)) {
        m_rmsWaterPlot->replot(QCustomPlot::rpQueuedReplot);
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口数据接收与绘制调度层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowData::onReceiveFFTData(QVector<float> data, int length, int frequency)
{
    if (m_shuttingDown) return;
    if (!m_fftWaterPlot || data.isEmpty() || length <= 0) {
        qWarning() << "Invalid FFT plot/data: plot null or data empty";
        return;
    }
    const int safeLen = qMax(0, qMin(length, data.size()));
    if (safeLen <= 0) return;
    const bool freqChanged = (m_fftFrequency != frequency);
    m_fftFrequency = frequency;
    if (freqChanged) {
        // 频率档位切换时回到默认视角，避免旧视角与新频率范围不一致
        m_fftAxisLockedByUser = false;
    }

    if (m_fftLineMode) {
        if (m_fftWaterPlot->graphCount() <= 0 || !m_fftWaterPlot->graph(0)) {
            qWarning() << "FFT spectrum mode graph is missing";
            return;
        }

        QVector<double> keys(safeLen), values(safeLen);
        const double freqStep = (safeLen > 1) ? (frequency / 2.0) / static_cast<double>(safeLen - 1) : 0.0;
        double maxAmp = 0.0;
        for (int i = 0; i < safeLen; ++i) {
            keys[i] = i * freqStep;
            values[i] = data[i];
            if (values[i] > maxAmp) maxAmp = values[i];
        }

        m_fftWaterPlot->graph(0)->setData(keys, values, true);
        if (!m_fftAxisLockedByUser) {
            m_fftApplyingAxisUpdate = true;
            m_fftWaterPlot->xAxis->setRange(0, frequency / 2.0);
            m_fftWaterPlot->yAxis->setRange(0, qMax(1.0, maxAmp * 1.15));
            m_fftApplyingAxisUpdate = false;
        }
        if (shouldReplot(m_fftReplotLimiter, kFftReplotMinIntervalMs)) {
            m_fftWaterPlot->replot(QCustomPlot::rpQueuedReplot);
        }
        return;
    }

    if (m_fftWaterPlot->plottableCount() <= 0) {
        qWarning() << "FFT plot has no plottable";
        return;
    }

    QCPAbstractPlottable *plottable = m_fftWaterPlot->plottable(0);
    QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(plottable);
    if (!colorMap) {
        qWarning() << "Plottable at index 0 is not a QCPColorMap";
        return;
    }

    QCPColorMapData *mapData = colorMap->data();
    if (!mapData) {
        qWarning() << "QCPColorMap data is null for FFT plot";
        return;
    }

    const bool configChanged = (freqChanged || mapData->keySize() != safeLen);

    if (configChanged) {
        const int fftCols = qMax(1, safeLen);
        if (!m_fftAxisLockedByUser) {
            m_fftApplyingAxisUpdate = true;
            m_fftWaterPlot->xAxis->setRange(0, m_fftFrequency / 2);
            m_fftWaterPlot->yAxis->setRange(0, 100);
            m_fftApplyingAxisUpdate = false;
        }
        mapData->setSize(fftCols, 100);
        mapData->setRange(QCPRange(0, m_fftFrequency / 2 + 2), QCPRange(0, 100));
    }

    const int cols = qMin(safeLen, mapData->keySize());
    const int rows = qMin(100, mapData->valueSize());
    if (cols <= 0 || rows <= 0) {
        qWarning() << "Invalid FFT colormap size:" << cols << rows;
        return;
    }

    for (int y = rows - 1; y > 0; --y) {
        for (int x = 0; x < cols; ++x) {
            mapData->setCell(x, y, mapData->cell(x, y - 1));
        }
    }

    for (int x = 0; x < cols; ++x) {
        mapData->setCell(x, 0, data[x]);
    }

    if (shouldReplot(m_fftReplotLimiter, kFftReplotMinIntervalMs)) {
        m_fftWaterPlot->replot(QCustomPlot::rpQueuedReplot);
    }
}




