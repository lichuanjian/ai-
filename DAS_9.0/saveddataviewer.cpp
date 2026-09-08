/**
 * @file saveddataviewer.cpp
 * @brief 离线加载保存目录，异步构建相位瀑布、通道波形和 FFT。
 *
 * 文件元数据从目录名/文件大小推断；后台任务读取 .bin、按用户频带进行 IIR 带通和抽样，
 * 再把可视化结果返回 GUI 线程。离线加载期间置位 saved_data_viewer_busy，使实时采集链短暂停止，
 * 防止大文件读取与 PCIe/内存带宽竞争。
 */
#include "saveddataviewer.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMutexLocker>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <new>
#include <atomic>

#include <fftw3.h>
#include "ParameterManager.h"
#include "fftw_guard.h"
#include "mainwindowutils.h"

namespace {
constexpr qsizetype kSingleWaveformMaxPlotSamples = 250000;
constexpr qsizetype kSaveAllWaveformMaxPlotSamples = 250000;
constexpr qint64 kMaxFftSamples = 1 << 20;
constexpr int kMaxWaterfallTimeCells = 2400;
constexpr int kMaxWaterfallDistanceCells = 4200;
constexpr double kPi = 3.14159265358979323846;
std::atomic_int g_savedDataOfflineLoadCount{0};

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void beginSavedDataOfflineLoad()
{
    if (g_savedDataOfflineLoadCount.fetch_add(1, std::memory_order_acq_rel) == 0) {
        ParameterManager::instance().setParameter(QStringLiteral("saved_data_viewer_busy"), true);
    }
}

/**
 * @brief 清理当前处理阶段的临时状态与持有资源，恢复可预测的后续运行条件。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void endSavedDataOfflineLoad()
{
    const int previous = g_savedDataOfflineLoadCount.fetch_sub(1, std::memory_order_acq_rel);
    if (previous <= 1) {
        g_savedDataOfflineLoadCount.store(0, std::memory_order_release);
        ParameterManager::instance().setParameter(QStringLiteral("saved_data_viewer_busy"), false);
    }
}

struct SavedFolderMeta
{
    bool isAllData = false;
    bool valid = false;
    QString folderPath;
    QString folderName;
    QString timestamp;
    int frequency = 0;
    int extractCount = 1;
    int differentialDistance = 1;
    int startChannel = 0;
    int endChannel = 0;
    int rows = 0;
    int cols = 0;
    double pitchMeters = 0.0;
    double intervalMeters = 0.0;
    double startMeters = 0.0;
    double endMeters = 0.0;
};

struct ChannelAnalysisResult
{
    bool ok = false;
    QString errorMessage;
    QString binPath;
    int channelRow = 0;
    qint64 totalSamples = 0;
    qsizetype plotStep = 1;
    int fftSampleCount = 0;
    QVector<double> waveformKeys;
    QVector<double> waveformValues;
    QVector<double> fftKeys;
    QVector<double> fftValues;
    double waveformMin = 0.0;
    double waveformMax = 0.0;
    double fftMin = 0.0;
    double fftMax = 0.0;
};

struct SaveAllBinResult
{
    bool ok = false;
    QString errorMessage;
    QString binPath;
    SavedFolderMeta meta;
    int channelRow = 0;
    qint64 frameCount = 0;
    int timeCells = 0;
    int distanceCells = 0;
    int frameStep = 1;
    int rowStep = 1;
    qint64 ignoredTailBytes = 0;
    double durationSeconds = 0.0;
    double distanceStart = 0.0;
    double distanceEnd = 0.0;
    double bandLowerHz = 0.0;
    double bandUpperHz = 0.0;
    double colorMin = 0.0;
    double colorMax = 0.0;
    QVector<double> waterfallValues;
    ChannelAnalysisResult channel;
};

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
QString formatSeconds(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const qint64 totalSeconds = static_cast<qint64>(std::llround(seconds));
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 secs = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

SavedFolderMeta parseSavedFolderMeta(const QString& folderPath)
{
    SavedFolderMeta meta;
    meta.folderPath = folderPath;
    meta.folderName = QFileInfo(folderPath).fileName();

    const QRegularExpression re(
        QStringLiteral(R"(^(data_all_|data_)(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})_freq(\d+)Hz_ext(\d+)_diff(\d+)_startCh(\d+)_endCh(\d+)_rows(\d+)_cols(\d+)_pitch([-0-9.]+)m_interval([-0-9.]+)m_startM([-0-9.]+)_endM([-0-9.]+)$)"));
    const QRegularExpressionMatch match = re.match(meta.folderName);
    if (!match.hasMatch()) {
        return meta;
    }

    meta.isAllData = (match.captured(1) == QStringLiteral("data_all_"));
    meta.timestamp = match.captured(2);
    meta.frequency = match.captured(3).toInt();
    meta.extractCount = match.captured(4).toInt();
    meta.differentialDistance = match.captured(5).toInt();
    meta.startChannel = match.captured(6).toInt();
    meta.endChannel = match.captured(7).toInt();
    meta.rows = match.captured(8).toInt();
    meta.cols = match.captured(9).toInt();
    meta.pitchMeters = match.captured(10).toDouble();
    meta.intervalMeters = match.captured(11).toDouble();
    meta.startMeters = match.captured(12).toDouble();
    meta.endMeters = match.captured(13).toDouble();
    meta.valid = meta.frequency > 0 && meta.cols > 0;
    return meta;
}

QStringList sortedBinFiles(const QString& folderPath)
{
    QDir dir(folderPath);
    QFileInfoList infos = dir.entryInfoList(QStringList() << QStringLiteral("*.bin"),
                                            QDir::Files | QDir::NoSymLinks,
                                            QDir::Name);
    std::sort(infos.begin(), infos.end(), [](const QFileInfo& left, const QFileInfo& right) {
        static const QRegularExpression indexRe(QStringLiteral(R"(_(\d+)\.bin$)"));
        const auto leftMatch = indexRe.match(left.fileName());
        const auto rightMatch = indexRe.match(right.fileName());
        const int leftIndex = leftMatch.hasMatch() ? leftMatch.captured(1).toInt() : 0;
        const int rightIndex = rightMatch.hasMatch() ? rightMatch.captured(1).toInt() : 0;
        if (leftIndex != rightIndex) return leftIndex < rightIndex;
        return left.fileName() < right.fileName();
    });

    QStringList files;
    files.reserve(infos.size());
    for (const QFileInfo& info : infos) {
        files.push_back(info.absoluteFilePath());
    }
    return files;
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
int inferActualRowsForBin(const SavedFolderMeta& meta, qint64 fileSize)
{
    if (meta.rows <= 0 || meta.cols <= 0 || fileSize <= 0) {
        return meta.rows;
    }
    const qint64 rowBytes = static_cast<qint64>(meta.cols) * static_cast<qint64>(sizeof(float));
    if (rowBytes <= 0 || (fileSize % rowBytes) != 0) {
        return meta.rows;
    }

    const qint64 totalRowBlocks = fileSize / rowBytes;
    if (totalRowBlocks <= 0) {
        return meta.rows;
    }
    if ((totalRowBlocks % meta.rows) == 0) {
        return meta.rows;
    }

    constexpr int kMaxRowInferenceDelta = 64;
    for (int delta = 1; delta <= kMaxRowInferenceDelta; ++delta) {
        const int lowerCandidate = meta.rows - delta;
        if (lowerCandidate > 0 && (totalRowBlocks % lowerCandidate) == 0) {
            return lowerCandidate;
        }
        const int upperCandidate = meta.rows + delta;
        if (upperCandidate > 0 && (totalRowBlocks % upperCandidate) == 0) {
            return upperCandidate;
        }
    }

    return meta.rows;
}

SavedFolderMeta effectiveMetaForBin(const SavedFolderMeta& meta, qint64 fileSize, bool *rowsAdjusted = nullptr)
{
    SavedFolderMeta effective = meta;
    const int actualRows = inferActualRowsForBin(meta, fileSize);
    if (rowsAdjusted) {
        *rowsAdjusted = (actualRows > 0 && actualRows != meta.rows);
    }
    if (actualRows > 0) {
        effective.rows = actualRows;
    }
    return effective;
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void removePlotColorScales(QCustomPlot *plot)
{
    if (!plot) return;
    QCPLayoutGrid *layoutGrid = qobject_cast<QCPLayoutGrid*>(plot->plotLayout());
    if (!layoutGrid) return;

    for (int i = layoutGrid->elementCount() - 1; i >= 0; --i) {
        QCPLayoutElement *elem = layoutGrid->elementAt(i);
        QCPColorScale *scale = qobject_cast<QCPColorScale*>(elem);
        if (!scale) continue;
        layoutGrid->takeAt(i);
        delete scale;
    }
    layoutGrid->simplify();
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void setPlotTitle(QCustomPlot *plot, const QString& title)
{
    if (!plot) return;
    QCPLayoutGrid *layoutGrid = qobject_cast<QCPLayoutGrid*>(plot->plotLayout());
    if (!layoutGrid) return;

    QCPTextElement *firstTitle = nullptr;
    for (int i = layoutGrid->elementCount() - 1; i >= 0; --i) {
        QCPLayoutElement *elem = layoutGrid->elementAt(i);
        QCPTextElement *titleElem = qobject_cast<QCPTextElement*>(elem);
        if (!titleElem) continue;
        if (!firstTitle) {
            firstTitle = titleElem;
        } else {
            layoutGrid->takeAt(i);
            delete titleElem;
        }
    }

    if (!firstTitle) {
        firstTitle = new QCPTextElement(plot);
        firstTitle->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 12, QFont::DemiBold));
        layoutGrid->insertRow(0);
        layoutGrid->addElement(0, 0, firstTitle);
    }
    firstTitle->setTextColor(QColor(22, 49, 77));
    firstTitle->setText(title);
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
QCPColorGradient strainGradient()
{
    QCPColorGradient gradient;
    gradient.setColorStopAt(0.00, QColor(44, 71, 181));
    gradient.setColorStopAt(0.15, QColor(70, 98, 206));
    gradient.setColorStopAt(0.35, QColor(133, 153, 229));
    gradient.setColorStopAt(0.50, QColor(245, 245, 245));
    gradient.setColorStopAt(0.65, QColor(236, 170, 170));
    gradient.setColorStopAt(0.85, QColor(220, 88, 88));
    gradient.setColorStopAt(1.00, QColor(205, 36, 36));
    return gradient;
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void recomputeSaveAllColorRange(SaveAllBinResult *result)
{
    if (!result || result->timeCells <= 0 || result->distanceCells <= 0) return;

    result->colorMin = std::numeric_limits<double>::max();
    result->colorMax = -std::numeric_limits<double>::max();
    for (int t = 0; t < result->timeCells; ++t) {
        const qsizetype offset = static_cast<qsizetype>(t) * result->distanceCells;
        for (int d = 0; d < result->distanceCells; ++d) {
            const double value = result->waterfallValues[offset + d];
            if (!std::isfinite(value)) continue;
            if (value < result->colorMin) result->colorMin = value;
            if (value > result->colorMax) result->colorMax = value;
        }
    }

    if (!std::isfinite(result->colorMin) || !std::isfinite(result->colorMax)) {
        result->colorMin = -1.0;
        result->colorMax = 1.0;
    }
    if (qFuzzyCompare(result->colorMin, result->colorMax)) {
        result->colorMin -= 0.5;
        result->colorMax += 0.5;
    }
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void applySaveAllBandpass(SaveAllBinResult *result, int sampleRate)
{
    if (!result || result->timeCells < 2 || result->distanceCells <= 0 || sampleRate <= 0) return;

    const double nyquist = sampleRate * 0.5;
    const double lower = qBound(0.0, result->bandLowerHz, nyquist);
    const double upper = qBound(0.0, result->bandUpperHz, nyquist);
    if (!(upper > lower)) {
        recomputeSaveAllColorRange(result);
        return;
    }

    const int n = result->timeCells;
    struct BiquadFilter {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double z1 = 0.0;
        double z2 = 0.0;

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
        double process(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    auto makeFilter = [sampleRate](double cutoff, bool highPass) {
        BiquadFilter filter;
        const double nyq = sampleRate * 0.5;
        cutoff = qBound(1e-6, cutoff, qMax(1e-6, nyq - 1e-6));
        const double omega = 2.0 * kPi * cutoff / sampleRate;
        const double sinw = std::sin(omega);
        const double cosw = std::cos(omega);
        const double q = std::sqrt(0.5);
        const double alpha = sinw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        double b0 = 0.0;
        double b1 = 0.0;
        double b2 = 0.0;
        if (highPass) {
            b0 = (1.0 + cosw) * 0.5;
            b1 = -(1.0 + cosw);
            b2 = (1.0 + cosw) * 0.5;
        } else {
            b0 = (1.0 - cosw) * 0.5;
            b1 = 1.0 - cosw;
            b2 = (1.0 - cosw) * 0.5;
        }
        filter.b0 = b0 / a0;
        filter.b1 = b1 / a0;
        filter.b2 = b2 / a0;
        filter.a1 = (-2.0 * cosw) / a0;
        filter.a2 = (1.0 - alpha) / a0;
        return filter;
    };

    for (int distanceIndex = 0; distanceIndex < result->distanceCells; ++distanceIndex) {
        BiquadFilter high = makeFilter(lower, true);
        BiquadFilter low = makeFilter(upper, false);
        for (int t = 0; t < n; ++t) {
            const qsizetype index = static_cast<qsizetype>(t) * result->distanceCells + distanceIndex;
            const double highPassed = high.process(result->waterfallValues[index]);
            result->waterfallValues[index] = low.process(highPassed);
        }
    }

    recomputeSaveAllColorRange(result);
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void styleSmoothWaveform(QCustomPlot *plot, QCPGraph *graph, const QColor& color)
{
    if (!plot || !graph) return;

    QPen pen(color, 1.45);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    graph->setPen(pen);
    graph->setAdaptiveSampling(true);
    graph->setAntialiased(true);

    plot->setNoAntialiasingOnDrag(false);
    plot->setPlottingHints(QCP::phCacheLabels);
    plot->setAntialiasedElements(QCP::aePlottables);
    plot->setNotAntialiasedElements(QCP::aeNone);
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool forEachFloatChunk(const QStringList& files,
                       qsizetype chunkFloats,
                       const std::function<bool(const float*, qsizetype)>& handler,
                       QString *errorMessage)
{
    const qsizetype safeChunkFloats = qMax<qsizetype>(1024, chunkFloats);
    QVector<float> buffer(safeChunkFloats);

    for (const QString& filePath : files) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法打开文件：%1").arg(filePath);
            }
            return false;
        }
        if ((file.size() % static_cast<qint64>(sizeof(float))) != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("文件大小不是 float 对齐：%1").arg(filePath);
            }
            return false;
        }

        while (true) {
            const qint64 maxBytes = static_cast<qint64>(buffer.size()) * static_cast<qint64>(sizeof(float));
            const qint64 bytesRead = file.read(reinterpret_cast<char*>(buffer.data()), maxBytes);
            if (bytesRead < 0) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("读取文件失败：%1").arg(filePath);
                }
                return false;
            }
            if (bytesRead == 0) {
                break;
            }
            if ((bytesRead % static_cast<qint64>(sizeof(float))) != 0) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("读取到的字节数不是 float 对齐：%1").arg(filePath);
                }
                return false;
            }

            const qsizetype floatCount = static_cast<qsizetype>(bytesRead / static_cast<qint64>(sizeof(float)));
            if (!handler(buffer.constData(), floatCount)) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
int chooseFftSampleCount(qint64 totalSamples)
{
    const qint64 capped = qMin<qint64>(totalSamples, kMaxFftSamples);
    if (capped <= 1) return 0;

    int fftSize = 1;
    while ((fftSize << 1) > 0 && (fftSize << 1) <= capped) {
        fftSize <<= 1;
    }
    return qMax(2, fftSize);
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
QString buildMetaText(const SavedFolderMeta& meta, const QStringList& extraLines)
{
    QStringList lines;
    lines << QStringLiteral("文件夹：%1").arg(meta.folderName);
    lines << QStringLiteral("参数：freq=%1Hz | ext=%2 | diff=%3 | startCh=%4 | endCh=%5 | rows=%6 | cols=%7")
                 .arg(meta.frequency)
                 .arg(meta.extractCount)
                 .arg(meta.differentialDistance)
                 .arg(meta.startChannel)
                 .arg(meta.endChannel)
                 .arg(meta.rows)
                 .arg(meta.cols);
    lines << QStringLiteral("距离：pitch=%1 m | interval=%2 m | start=%3 m | end=%4 m")
                 .arg(meta.pitchMeters, 0, 'f', 2)
                 .arg(meta.intervalMeters, 0, 'f', 2)
                 .arg(meta.startMeters, 0, 'f', 2)
                 .arg(meta.endMeters, 0, 'f', 2);
    lines += extraLines;
    return lines.join(QStringLiteral("\n"));
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
int channelRowFromValue(const SavedFolderMeta& meta, int channelValue)
{
    const int maxRow = qMax(0, meta.rows - 1);
    if (meta.startChannel > 0 || channelValue >= meta.startChannel) {
        return qBound(0, channelValue - meta.startChannel, maxRow);
    }
    return qBound(0, channelValue, maxRow);
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
int channelValueFromRow(const SavedFolderMeta& meta, int channelRow)
{
    return meta.startChannel + qBound(0, channelRow, qMax(0, meta.rows - 1));
}

/**
 * @brief 执行本阶段的数据计算与转换，并保持输入输出序列的既有约束。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void calculateFft(ChannelAnalysisResult *result,
                  const QVector<float>& fftBuffer,
                  int sampleRate,
                  double maxFrequency)
{
    if (!result || fftBuffer.size() < 2 || sampleRate <= 0) return;

    QVector<float> fftInput(fftBuffer.size());
    for (int i = 0; i < fftBuffer.size(); ++i) {
        const double phase = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(fftBuffer.size() - 1);
        const float window = static_cast<float>(0.5 * (1.0 - std::cos(phase)));
        fftInput[i] = fftBuffer[i] * window;
    }

    fftwf_complex *output = reinterpret_cast<fftwf_complex*>(
        fftwf_malloc((fftBuffer.size() / 2 + 1) * sizeof(fftwf_complex)));
    fftwf_plan plan = nullptr;
    if (output) {
        QMutexLocker plannerLocker(&fftwPlannerMutex());
        plan = fftwf_plan_dft_r2c_1d(fftInput.size(), fftInput.data(), output, FFTW_ESTIMATE);
    }

    if (!output || !plan) {
        if (plan) fftwf_destroy_plan(plan);
        if (output) fftwf_free(output);
        return;
    }

    fftwf_execute(plan);
    const int bins = fftInput.size() / 2 + 1;
    const double scale = qMax(1.0, static_cast<double>(fftInput.size()));
    result->fftMin = std::numeric_limits<double>::max();
    result->fftMax = -std::numeric_limits<double>::max();

    for (int i = 1; i < bins; ++i) {
        const double frequency = i * (static_cast<double>(sampleRate) / fftInput.size());
        if (frequency > maxFrequency) break;

        const double realPart = output[i][0];
        const double imagPart = output[i][1];
        const double magnitude = std::sqrt(realPart * realPart + imagPart * imagPart) / scale;
        const double db = 20.0 * std::log10(qMax(1e-12, magnitude));

        result->fftKeys.push_back(frequency);
        result->fftValues.push_back(db);
        if (db < result->fftMin) result->fftMin = db;
        if (db > result->fftMax) result->fftMax = db;
    }

    if (result->fftKeys.isEmpty()) {
        result->fftMin = 0.0;
        result->fftMax = 0.0;
    }

    {
        QMutexLocker plannerLocker(&fftwPlannerMutex());
        fftwf_destroy_plan(plan);
    }
    fftwf_free(output);
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
ChannelAnalysisResult readChannelAnalysis(const QString& binPath,
                                          const SavedFolderMeta& meta,
                                          int channelRow)
{
    ChannelAnalysisResult result;
    result.binPath = binPath;
    result.channelRow = channelRow;

    if (!meta.valid || !meta.isAllData || meta.rows <= 0 || meta.cols <= 0 || meta.frequency <= 0) {
        result.errorMessage = QStringLiteral("保存全部数据参数无效。");
        return result;
    }

    QFile file(binPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("无法打开 bin 文件：%1").arg(binPath);
        return result;
    }

    const SavedFolderMeta effectiveMeta = effectiveMetaForBin(meta, file.size());
    channelRow = qBound(0, channelRow, qMax(0, effectiveMeta.rows - 1));
    result.channelRow = channelRow;

    const qint64 frameFloats = static_cast<qint64>(effectiveMeta.rows) * static_cast<qint64>(effectiveMeta.cols);
    const qint64 frameBytes = frameFloats * static_cast<qint64>(sizeof(float));
    if (frameFloats <= 0 || frameBytes <= 0 || frameFloats > std::numeric_limits<qsizetype>::max()) {
        result.errorMessage = QStringLiteral("rows/cols 参数过大，无法解析。");
        return result;
    }
    if ((file.size() % static_cast<qint64>(sizeof(float))) != 0) {
        result.errorMessage = QStringLiteral("bin 文件大小与 rows/cols 参数不匹配：%1").arg(binPath);
        return result;
    }

    const qint64 frameCount = file.size() / frameBytes;
    result.totalSamples = frameCount * static_cast<qint64>(effectiveMeta.cols);
    if (frameCount <= 0 || result.totalSamples <= 0) {
        result.errorMessage = QStringLiteral("bin 文件不足一个完整帧。");
        return result;
    }

    result.plotStep = qMax<qsizetype>(
        1,
        static_cast<qsizetype>((result.totalSamples + kSaveAllWaveformMaxPlotSamples - 1) / kSaveAllWaveformMaxPlotSamples));
    result.fftSampleCount = chooseFftSampleCount(result.totalSamples);
    const qint64 fftStartIndex = result.totalSamples - result.fftSampleCount;
    QVector<float> fftBuffer(result.fftSampleCount);
    QVector<float> frameBuffer(static_cast<qsizetype>(frameFloats));

    result.waveformKeys.reserve(static_cast<int>(qMin<qint64>(
        (result.totalSamples + result.plotStep - 1) / result.plotStep + 1,
        kSaveAllWaveformMaxPlotSamples + 1)));
    result.waveformValues.reserve(result.waveformKeys.capacity());

    bool haveWaveformValue = false;
    float lastSample = 0.0f;
    qint64 sampleIndex = 0;
    result.waveformMin = std::numeric_limits<double>::max();
    result.waveformMax = -std::numeric_limits<double>::max();

    for (qint64 frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const qint64 bytesRead = file.read(reinterpret_cast<char*>(frameBuffer.data()), frameBytes);
        if (bytesRead != frameBytes) {
            result.errorMessage = QStringLiteral("读取 bin 文件时遇到不完整帧：%1").arg(binPath);
            return result;
        }

        const qint64 rowOffset = static_cast<qint64>(channelRow) * static_cast<qint64>(effectiveMeta.cols);
        for (int c = 0; c < effectiveMeta.cols; ++c, ++sampleIndex) {
            const float value = frameBuffer[static_cast<qsizetype>(rowOffset + c)];
            lastSample = value;
            haveWaveformValue = true;
            if (value < result.waveformMin) result.waveformMin = value;
            if (value > result.waveformMax) result.waveformMax = value;

            if ((sampleIndex % result.plotStep) == 0) {
                result.waveformKeys.push_back(static_cast<double>(sampleIndex) / effectiveMeta.frequency);
                result.waveformValues.push_back(value);
            }
            if (result.fftSampleCount > 0 && sampleIndex >= fftStartIndex) {
                fftBuffer[static_cast<int>(sampleIndex - fftStartIndex)] = value;
            }
        }
    }

    if (!haveWaveformValue) {
        result.errorMessage = QStringLiteral("所选通道没有可用样本。");
        return result;
    }

    const double lastKey = static_cast<double>(result.totalSamples - 1) / effectiveMeta.frequency;
    if (result.waveformKeys.isEmpty() || !qFuzzyCompare(result.waveformKeys.last() + 1.0, lastKey + 1.0)) {
        result.waveformKeys.push_back(lastKey);
        result.waveformValues.push_back(lastSample);
    }

    if (qFuzzyCompare(result.waveformMin, result.waveformMax)) {
        result.waveformMin -= 0.5;
        result.waveformMax += 0.5;
    }

    calculateFft(&result, fftBuffer, effectiveMeta.frequency, 100.0);
    result.ok = true;
    return result;
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
SaveAllBinResult readSaveAllBin(const QString& binPath,
                                const SavedFolderMeta& meta,
                                int channelRow,
                                double bandLowerHz,
                                double bandUpperHz)
{
    SaveAllBinResult result;
    result.binPath = binPath;

    QFile file(binPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("无法打开 bin 文件：%1").arg(binPath);
        return result;
    }

    const SavedFolderMeta effectiveMeta = effectiveMetaForBin(meta, file.size());
    result.meta = effectiveMeta;
    result.bandLowerHz = bandLowerHz;
    result.bandUpperHz = bandUpperHz;
    channelRow = qBound(0, channelRow, qMax(0, effectiveMeta.rows - 1));
    result.channelRow = channelRow;

    const qint64 frameFloats = static_cast<qint64>(effectiveMeta.rows) * static_cast<qint64>(effectiveMeta.cols);
    const qint64 frameBytes = frameFloats * static_cast<qint64>(sizeof(float));
    if (frameFloats <= 0 || frameBytes <= 0 || frameFloats > std::numeric_limits<qsizetype>::max()) {
        result.errorMessage = QStringLiteral("rows/cols 参数过大，无法解析。");
        return result;
    }
    if ((file.size() % static_cast<qint64>(sizeof(float))) != 0) {
        result.errorMessage = QStringLiteral("目标 bin 文件大小与 rows/cols 参数不匹配：%1").arg(binPath);
        return result;
    }

    result.frameCount = file.size() / frameBytes;
    result.ignoredTailBytes = file.size() % frameBytes;
    if (result.frameCount <= 0) {
        result.errorMessage = QStringLiteral("目标 bin 文件不足一个完整帧。");
        return result;
    }

    const qint64 totalTimeSamples = result.frameCount * static_cast<qint64>(effectiveMeta.cols);
    result.frameStep = qMax<int>(
        1,
        static_cast<int>((totalTimeSamples + kMaxWaterfallTimeCells - 1) / kMaxWaterfallTimeCells));
    result.rowStep = qMax<int>(1, (effectiveMeta.rows + kMaxWaterfallDistanceCells - 1) / kMaxWaterfallDistanceCells);
    result.timeCells = static_cast<int>((totalTimeSamples + result.frameStep - 1) / result.frameStep);
    result.distanceCells = (effectiveMeta.rows + result.rowStep - 1) / result.rowStep;
    result.waterfallValues.resize(static_cast<qsizetype>(result.timeCells) * result.distanceCells);
    result.colorMin = std::numeric_limits<double>::max();
    result.colorMax = -std::numeric_limits<double>::max();

    QVector<float> frameBuffer(static_cast<qsizetype>(frameFloats));
    int timeCell = 0;
    for (qint64 frameIndex = 0; frameIndex < result.frameCount; ++frameIndex) {
        const qint64 bytesRead = file.read(reinterpret_cast<char*>(frameBuffer.data()), frameBytes);
        if (bytesRead != frameBytes) {
            result.errorMessage = QStringLiteral("读取 bin 文件时遇到不完整帧：%1").arg(binPath);
            return result;
        }
        for (int c = 0; c < effectiveMeta.cols; ++c) {
            const qint64 globalSampleIndex = frameIndex * static_cast<qint64>(effectiveMeta.cols) + c;
            if ((globalSampleIndex % result.frameStep) != 0) {
                continue;
            }
            if (timeCell >= result.timeCells) {
                break;
            }

            for (int y = 0; y < result.distanceCells; ++y) {
                const int row = qMin(effectiveMeta.rows - 1, y * result.rowStep);
                const qint64 rowOffset = static_cast<qint64>(row) * static_cast<qint64>(effectiveMeta.cols);
                const double value = frameBuffer[static_cast<qsizetype>(rowOffset + c)];
                result.waterfallValues[static_cast<qsizetype>(timeCell) * result.distanceCells + y] = value;
            }
            ++timeCell;
        }
    }

    result.timeCells = qMin(result.timeCells, timeCell);
    if (result.timeCells <= 0 || result.distanceCells <= 0) {
        result.errorMessage = QStringLiteral("目标 bin 没有可显示的数据。");
        return result;
    }

    result.durationSeconds = (static_cast<double>(result.frameCount) * effectiveMeta.cols) / effectiveMeta.frequency;
    result.distanceStart = effectiveMeta.intervalMeters > 0.0 ? effectiveMeta.startMeters : static_cast<double>(effectiveMeta.startChannel);
    if (effectiveMeta.intervalMeters > 0.0) {
        const int lastRow = qMin(effectiveMeta.rows - 1, (result.distanceCells - 1) * result.rowStep);
        result.distanceEnd = effectiveMeta.startMeters + lastRow * effectiveMeta.intervalMeters;
    } else {
        result.distanceEnd = static_cast<double>(effectiveMeta.startChannel + qMin(effectiveMeta.rows - 1, (result.distanceCells - 1) * result.rowStep));
    }
    if (qFuzzyCompare(result.distanceStart + 1.0, result.distanceEnd + 1.0)) {
        result.distanceEnd = result.distanceStart + 1.0;
    }

    const int filteredSampleRate = qMax(1, static_cast<int>(std::llround(static_cast<double>(effectiveMeta.frequency) / result.frameStep)));
    applySaveAllBandpass(&result, filteredSampleRate);

    result.ok = true;
    return result;
}
} // namespace

/**
 * @brief 按当前保存策略写入或保留数据，并沿用既有的格式和错误处理。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
SavedDataViewer::SavedDataViewer(QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint | Qt::WindowMaximizeButtonHint);
    setSizeGripEnabled(true);
    setMinimumSize(1180, 820);
    resize(1500, 930);
    setModal(false);

    m_workerPool = new QThreadPool(this);
    m_workerPool->setMaxThreadCount(1);
    m_workerPool->setExpiryTimeout(-1);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setStyleSheet(QStringLiteral("font: 700 15pt \"Microsoft YaHei UI\"; color:#16314d;"));

    m_infoLabel = new QLabel(this);
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setMaximumHeight(92);
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_infoLabel->setStyleSheet(QStringLiteral("background:#f4f8fd; border:1px solid #cddced; border-radius:10px; padding:10px; color:#35506c;"));

    m_saveAllControls = new QWidget(this);
    QHBoxLayout *controlLayout = new QHBoxLayout(m_saveAllControls);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(8);

    QLabel *channelLabel = new QLabel(QStringLiteral("FFT通道"), m_saveAllControls);
    m_channelSpin = new QSpinBox(m_saveAllControls);
    m_channelSpin->setMinimum(0);
    m_channelSpin->setMaximum(0);
    m_channelSpin->setFixedWidth(100);
    channelLabel->setVisible(false);
    m_channelSpin->setVisible(false);
    if (m_channelButton) m_channelButton->setVisible(false);
    m_channelButton = new QPushButton(QStringLiteral("查看通道"), m_saveAllControls);

    QLabel *bandLabel = new QLabel(QStringLiteral("频段"), m_saveAllControls);
    m_channelButton->setVisible(false);
    m_fftBandCombo = new QComboBox(m_saveAllControls);
    m_fftBandCombo->addItem(QStringLiteral("0.001Hz - 1Hz"), QVariantList{0.001, 1.0});
    m_fftBandCombo->addItem(QStringLiteral("1Hz - 10Hz"), QVariantList{1.0, 10.0});
    m_fftBandCombo->addItem(QStringLiteral("10Hz - 100Hz"), QVariantList{10.0, 100.0});
    m_fftBandCombo->setFixedWidth(150);

    controlLayout->addWidget(channelLabel);
    controlLayout->addWidget(m_channelSpin);
    controlLayout->addWidget(m_channelButton);
    controlLayout->addSpacing(18);
    controlLayout->addWidget(bandLabel);
    controlLayout->addWidget(m_fftBandCombo);
    controlLayout->addStretch(1);

    connect(m_channelButton, &QPushButton::clicked, this, &SavedDataViewer::onSaveAllChannelRequested);
    connect(m_channelSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this]() {
        if (m_channelButton) m_channelButton->setEnabled(!m_saveAllBinPath.isEmpty());
    });
    connect(m_fftBandCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &SavedDataViewer::onFftBandChanged);

    m_primaryPlot = new QCustomPlot(this);
    m_secondaryPlot = new QCustomPlot(this);
    m_tertiaryPlot = new QCustomPlot(this);
    m_closeButton = new QPushButton(QStringLiteral("关闭"), this);
    m_closeButton->setFixedWidth(88);
    m_closeButton->setStyleSheet(QStringLiteral(
        "QPushButton{background:#1f6fd6; color:white; border:none; border-radius:8px; padding:6px 12px; font-weight:600;}"
        "QPushButton:hover{background:#195caf;}"));

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);

    mainLayout->addWidget(m_headerLabel);
    mainLayout->addWidget(m_infoLabel);
    mainLayout->addWidget(m_saveAllControls);
    mainLayout->addWidget(m_primaryPlot, 4);
    mainLayout->addWidget(m_secondaryPlot, 2);
    mainLayout->addWidget(m_tertiaryPlot, 2);

    QHBoxLayout *footerLayout = new QHBoxLayout;
    footerLayout->addStretch(1);
    footerLayout->addWidget(m_closeButton);
    mainLayout->addLayout(footerLayout);

    setSaveAllControlsVisible(false);
    setPlotVisibleCount(2);
}

/**
 * @brief 完成对象析构时的停止、断连和资源释放收尾。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
SavedDataViewer::~SavedDataViewer()
{
    if (m_workerPool) {
        m_workerPool->waitForDone();
    }
    endOfflineLoadGuard();
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::beginOfflineLoadGuard()
{
    if (m_offlineLoadActive) return;
    m_offlineLoadActive = true;
    beginSavedDataOfflineLoad();
}

/**
 * @brief 清理当前处理阶段的临时状态与持有资源，恢复可预测的后续运行条件。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::endOfflineLoadGuard()
{
    if (!m_offlineLoadActive) return;
    m_offlineLoadActive = false;
    endSavedDataOfflineLoad();
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::setHeaderTexts(const QString& title, const QString& infoText)
{
    setWindowTitle(title);
    if (m_headerLabel) m_headerLabel->setText(title);
    if (m_infoLabel) m_infoLabel->setText(infoText);
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::prepareGraphPlot(QCustomPlot *plot, const QString& title, const QString& xLabel, const QString& yLabel)
{
    if (!plot) return;

    plot->clearPlottables();
    plot->clearItems();
    removePlotColorScales(plot);
    MainWindowUtils::setPlotBackground(plot);
    setPlotTitle(plot, title);
    plot->legend->setVisible(false);
    plot->xAxis->setLabel(xLabel);
    plot->yAxis->setLabel(yLabel);
    plot->yAxis->setRangeReversed(false);
    plot->axisRect()->setupFullAxesBox();
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot->setNoAntialiasingOnDrag(true);
    plot->setPlottingHints(QCP::phFastPolylines | QCP::phCacheLabels);
    plot->setAntialiasedElements(QCP::aeNone);
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::setPlotVisibleCount(int count)
{
    if (m_primaryPlot) m_primaryPlot->setVisible(count >= 1);
    if (m_secondaryPlot) m_secondaryPlot->setVisible(count >= 2);
    if (m_tertiaryPlot) m_tertiaryPlot->setVisible(count >= 3);
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::setSaveAllControlsVisible(bool visible)
{
    if (m_saveAllControls) m_saveAllControls->setVisible(visible);
}

/**
 * @brief 建立本功能需要的文件、设备、缓存或界面状态。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
int SavedDataViewer::selectedSaveAllRow() const
{
    if (!m_channelSpin) return 0;
    SavedFolderMeta meta;
    meta.valid = true;
    meta.isAllData = true;
    meta.rows = m_saveAllRows;
    meta.cols = m_saveAllCols;
    meta.frequency = m_saveAllFrequency;
    meta.startChannel = m_saveAllStartChannel;
    meta.endChannel = m_saveAllEndChannel;
    return channelRowFromValue(meta, m_channelSpin->value());
}

/**
 * @brief 建立本功能需要的文件、设备、缓存或界面状态。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
QPair<double, double> SavedDataViewer::selectedBandpassRange() const
{
    if (!m_fftBandCombo) return qMakePair(0.001, 1.0);
    const QVariant data = m_fftBandCombo->currentData();
    const QVariantList list = data.toList();
    if (list.size() >= 2) {
        const double lower = list[0].toDouble();
        const double upper = list[1].toDouble();
        if (upper > lower) return qMakePair(lower, upper);
    }
    switch (m_fftBandCombo->currentIndex()) {
    case 1:
        return qMakePair(1.0, 10.0);
    case 2:
        return qMakePair(10.0, 100.0);
    default:
        return qMakePair(0.001, 1.0);
    }
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool SavedDataViewer::loadSinglePointFolder(const QString& folderPath, QString *errorMessage)
{
    setSaveAllControlsVisible(false);

    const SavedFolderMeta meta = parseSavedFolderMeta(folderPath);
    if (!meta.valid) {
        if (errorMessage) *errorMessage = QStringLiteral("无法从文件夹名称解析单点保存参数。");
        return false;
    }
    if (meta.isAllData) {
        if (errorMessage) *errorMessage = QStringLiteral("当前文件夹是“保存全部”数据，请使用“查看保存全部”。");
        return false;
    }

    const QStringList files = sortedBinFiles(folderPath);
    if (files.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("该文件夹下没有找到 .bin 数据文件。");
        return false;
    }

    qint64 totalSamples = 0;
    for (const QString& filePath : files) {
        QFileInfo info(filePath);
        totalSamples += info.size() / static_cast<qint64>(sizeof(float));
    }
    if (totalSamples <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("单点保存数据为空。");
        return false;
    }

    beginOfflineLoadGuard();
    QApplication::setOverrideCursor(Qt::WaitCursor);

    prepareGraphPlot(m_primaryPlot, QStringLiteral("单点波形图"), QStringLiteral("时间(hh:mm:ss)"), QStringLiteral("相位(rad)"));
    prepareGraphPlot(m_secondaryPlot, QStringLiteral("单点 FFT 频谱图"), QStringLiteral("频率(Hz)"), QStringLiteral("幅值(dB)"));
    setPlotVisibleCount(2);

    m_primaryPlot->addGraph();
    styleSmoothWaveform(m_primaryPlot, m_primaryPlot->graph(0), QColor(40, 110, 255));
    QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
    timeTicker->setTimeFormat(QStringLiteral("%h:%m:%s"));
    m_primaryPlot->xAxis->setTicker(timeTicker);

    const qsizetype plotStep = qMax<qsizetype>(1, (totalSamples + kSingleWaveformMaxPlotSamples - 1) / kSingleWaveformMaxPlotSamples);
    const int fftSampleCount = chooseFftSampleCount(totalSamples);
    const qint64 fftStartIndex = totalSamples - fftSampleCount;

    QVector<double> waveKeys;
    QVector<double> waveValues;
    waveKeys.reserve(static_cast<int>(qMin<qint64>((totalSamples + plotStep - 1) / plotStep + 1, 300000)));
    waveValues.reserve(waveKeys.capacity());

    QVector<float> fftBuffer(fftSampleCount);
    qint64 sampleIndex = 0;
    float lastSample = 0.0f;
    float minValue = std::numeric_limits<float>::max();
    float maxValue = -std::numeric_limits<float>::max();

    QString chunkError;
    const bool ok = forEachFloatChunk(files, 65536,
                                      [&](const float *chunk, qsizetype count) {
                                          for (qsizetype i = 0; i < count; ++i, ++sampleIndex) {
                                              const float value = chunk[i];
                                              lastSample = value;
                                              if (value < minValue) minValue = value;
                                              if (value > maxValue) maxValue = value;

                                              if ((sampleIndex % plotStep) == 0) {
                                                  waveKeys.push_back(static_cast<double>(sampleIndex) / meta.frequency);
                                                  waveValues.push_back(value);
                                              }

                                              if (fftSampleCount > 0 && sampleIndex >= fftStartIndex) {
                                                  fftBuffer[static_cast<int>(sampleIndex - fftStartIndex)] = value;
                                              }
                                          }
                                          return true;
                                      },
                                      &chunkError);

    if (!ok) {
        QApplication::restoreOverrideCursor();
        endOfflineLoadGuard();
        if (errorMessage) *errorMessage = chunkError;
        return false;
    }

    if (sampleIndex <= 0) {
        QApplication::restoreOverrideCursor();
        endOfflineLoadGuard();
        if (errorMessage) *errorMessage = QStringLiteral("单点保存数据为空。");
        return false;
    }

    const double totalDuration = static_cast<double>(sampleIndex) / meta.frequency;
    const double lastKey = static_cast<double>(sampleIndex - 1) / meta.frequency;
    if (waveKeys.isEmpty() || !qFuzzyCompare(waveKeys.last() + 1.0, lastKey + 1.0)) {
        waveKeys.push_back(lastKey);
        waveValues.push_back(lastSample);
    }

    m_primaryPlot->graph(0)->setData(waveKeys, waveValues, true);
    m_primaryPlot->xAxis->setRange(0, qMax(1.0, totalDuration));
    if (qFuzzyCompare(minValue, maxValue)) {
        minValue -= 0.5f;
        maxValue += 0.5f;
    }
    const double wavePad = qMax(0.08, (maxValue - minValue) * 0.15);
    m_primaryPlot->yAxis->setRange(minValue - wavePad, maxValue + wavePad);
    m_primaryPlot->replot(QCustomPlot::rpQueuedReplot);

    m_secondaryPlot->addGraph();
    m_secondaryPlot->graph(0)->setPen(QPen(QColor(255, 142, 54), 1.15));
    m_secondaryPlot->graph(0)->setAdaptiveSampling(true);
    m_secondaryPlot->graph(0)->setAntialiased(false);

    ChannelAnalysisResult fftResult;
    fftResult.fftSampleCount = fftSampleCount;
    calculateFft(&fftResult, fftBuffer, meta.frequency, meta.frequency / 2.0);
    if (!fftResult.fftKeys.isEmpty()) {
        m_secondaryPlot->graph(0)->setData(fftResult.fftKeys, fftResult.fftValues, true);
        m_secondaryPlot->xAxis->setRange(0, qMin(100.0, meta.frequency / 2.0));
        if (qFuzzyCompare(fftResult.fftMin, fftResult.fftMax)) {
            fftResult.fftMin -= 3.0;
            fftResult.fftMax += 3.0;
        }
        const double fftPad = qMax(2.0, (fftResult.fftMax - fftResult.fftMin) * 0.12);
        m_secondaryPlot->yAxis->setRange(fftResult.fftMin - fftPad, fftResult.fftMax + fftPad);
        m_secondaryPlot->replot(QCustomPlot::rpQueuedReplot);
    }

    const qint64 frameCount = meta.cols > 0 ? (sampleIndex / meta.cols) : 0;
    setHeaderTexts(QStringLiteral("查看单点保存数据"),
                   buildMetaText(meta,
                                 QStringList()
                                     << QStringLiteral("统计：样本=%1 | 保存帧数=%2 | 时长=%3")
                                            .arg(sampleIndex)
                                            .arg(frameCount)
                                            .arg(formatSeconds(totalDuration))
                                     << QStringLiteral("显示：波形抽样步长=%1 | FFT样本=%2").arg(plotStep).arg(fftSampleCount)));

    QApplication::restoreOverrideCursor();
    endOfflineLoadGuard();
    return true;
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool SavedDataViewer::loadSaveAllFolder(const QString& folderPath, QString *errorMessage)
{
    const SavedFolderMeta meta = parseSavedFolderMeta(folderPath);
    if (!meta.valid) {
        if (errorMessage) *errorMessage = QStringLiteral("无法从文件夹名称解析“保存全部”参数。");
        return false;
    }
    if (!meta.isAllData) {
        if (errorMessage) *errorMessage = QStringLiteral("当前文件夹是单点保存数据，请使用“查看单点”。");
        return false;
    }
    if (meta.rows <= 0 || meta.cols <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("保存全部数据缺少有效的 rows/cols 参数。");
        return false;
    }

    const QStringList files = sortedBinFiles(folderPath);
    if (files.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("该文件夹下没有找到 .bin 数据文件。");
        return false;
    }

    return loadSaveAllBinFile(files.first(), errorMessage);
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool SavedDataViewer::loadSaveAllBinFile(const QString& binPath, QString *errorMessage)
{
    const QFileInfo binInfo(binPath);
    if (binPath.isEmpty() || !binInfo.exists() || !binInfo.isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("请选择有效的 .bin 文件。");
        return false;
    }
    if (binInfo.suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("请选择 .bin 数据文件。");
        return false;
    }

    return loadSaveAllBinFile(binInfo.absolutePath(), binInfo.absoluteFilePath(), errorMessage);
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool SavedDataViewer::loadSaveAllBinFile(const QString& folderPath, const QString& binPath, QString *errorMessage)
{
    const QFileInfo folderInfo(folderPath);
    const QFileInfo binInfo(binPath);
    if (folderPath.isEmpty() || !folderInfo.exists() || !folderInfo.isDir()) {
        if (errorMessage) *errorMessage = QStringLiteral("请选择有效的保存全部数据文件夹。");
        return false;
    }
    if (binPath.isEmpty() || !binInfo.exists() || !binInfo.isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("请选择有效的 .bin 文件。");
        return false;
    }
    if (binInfo.suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("请选择 .bin 数据文件。");
        return false;
    }
    if (binInfo.absolutePath() != folderInfo.absoluteFilePath()) {
        if (errorMessage) *errorMessage = QStringLiteral("请选择当前保存全部文件夹中的 bin 文件。");
        return false;
    }

    const SavedFolderMeta meta = parseSavedFolderMeta(folderInfo.absoluteFilePath());
    if (!meta.valid) {
        if (errorMessage) *errorMessage = QStringLiteral("无法从文件夹名称解析“保存全部”参数。");
        return false;
    }
    if (!meta.isAllData) {
        if (errorMessage) *errorMessage = QStringLiteral("当前文件夹是单点保存数据，请使用“查看单点”。");
        return false;
    }
    if (meta.rows <= 0 || meta.cols <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("保存全部数据缺少有效的 rows/cols 参数。");
        return false;
    }

    bool rowsAdjusted = false;
    const SavedFolderMeta effectiveMeta = effectiveMetaForBin(meta, binInfo.size(), &rowsAdjusted);
    const qint64 frameBytes = static_cast<qint64>(effectiveMeta.rows) * static_cast<qint64>(effectiveMeta.cols) * static_cast<qint64>(sizeof(float));
    if (frameBytes <= 0 || (binInfo.size() % static_cast<qint64>(sizeof(float))) != 0 || (binInfo.size() < frameBytes)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("所选 bin 文件大小与 rows/cols 参数不匹配：%1").arg(binInfo.fileName());
        }
        return false;
    }

    m_saveAllFolderPath = folderInfo.absoluteFilePath();
    m_saveAllBinPath = binInfo.absoluteFilePath();
    m_saveAllRows = effectiveMeta.rows;
    m_saveAllCols = effectiveMeta.cols;
    m_saveAllFrequency = effectiveMeta.frequency;
    m_saveAllStartChannel = effectiveMeta.startChannel;
    m_saveAllEndChannel = effectiveMeta.endChannel;
    m_currentFftKeys.clear();
    m_currentFftValues.clear();
    m_currentFftMinDb = 0.0;
    m_currentFftMaxDb = 0.0;

    if (m_channelSpin) {
        m_channelSpin->setRange(effectiveMeta.startChannel, effectiveMeta.startChannel + effectiveMeta.rows - 1);
        m_channelSpin->setValue(effectiveMeta.startChannel);
    }

    setSaveAllControlsVisible(true);
    setPlotVisibleCount(1);
    prepareGraphPlot(m_primaryPlot,
                     QStringLiteral("保存全部数据瀑布图"),
                     QStringLiteral("时间(hh:mm:ss)"),
                     effectiveMeta.intervalMeters > 0.0 ? QStringLiteral("距离(m)") : QStringLiteral("通道"));
    prepareGraphPlot(m_secondaryPlot, QStringLiteral("所选通道波形"), QStringLiteral("时间(hh:mm:ss)"), QStringLiteral("相位(rad)"));
    prepareGraphPlot(m_tertiaryPlot, QStringLiteral("所选通道 FFT"), QStringLiteral("频率(Hz)"), QStringLiteral("幅值(dB)"));
    QStringList loadingLines;
    loadingLines << QStringLiteral("当前 bin：%1").arg(binInfo.fileName());
    if (rowsAdjusted) {
        loadingLines << QStringLiteral("提示：文件夹名 rows=%1，已按 bin 实际帧行数 rows=%2 读取。")
                            .arg(meta.rows)
                            .arg(effectiveMeta.rows);
    }
    loadingLines << QStringLiteral("状态：正在后台线程读取单个 bin，不再扫描整个文件夹。");
    setHeaderTexts(QStringLiteral("查看保存全部数据"),
                   buildMetaText(effectiveMeta, loadingLines));

    startSaveAllBinLoad(m_saveAllBinPath, selectedSaveAllRow());
    return true;
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::startSaveAllBinLoad(const QString& binPath, int channelRow)
{
    if (!m_workerPool) return;
    beginOfflineLoadGuard();
    const auto bandRange = selectedBandpassRange();
    m_saveAllLoading = true;
    if (m_channelButton) m_channelButton->setEnabled(false);
    if (m_fftBandCombo) m_fftBandCombo->setEnabled(false);
    if (m_closeButton) m_closeButton->setEnabled(true);

    SavedFolderMeta meta = parseSavedFolderMeta(m_saveAllFolderPath);
    const auto future = QtConcurrent::run(m_workerPool, [binPath, meta, channelRow, bandRange]() {
        try {
            return readSaveAllBin(binPath, meta, channelRow, bandRange.first, bandRange.second);
        } catch (const std::bad_alloc&) {
            SaveAllBinResult result;
            result.binPath = binPath;
            result.meta = meta;
            result.errorMessage = QStringLiteral("内存不足，无法渲染该保存全部 bin。请换较短文件或降低保存时长。");
            return result;
        } catch (const std::exception& e) {
            SaveAllBinResult result;
            result.binPath = binPath;
            result.meta = meta;
            result.errorMessage = QStringLiteral("读取保存全部 bin 异常：%1").arg(QString::fromLocal8Bit(e.what()));
            return result;
        } catch (...) {
            SaveAllBinResult result;
            result.binPath = binPath;
            result.meta = meta;
            result.errorMessage = QStringLiteral("读取保存全部 bin 时发生未知异常。");
            return result;
        }
    });

    auto *watcher = new QFutureWatcher<SaveAllBinResult>(this);
    connect(watcher, &QFutureWatcher<SaveAllBinResult>::finished, this, [this, watcher]() {
        const SaveAllBinResult result = watcher->result();
        watcher->deleteLater();
        m_saveAllLoading = false;
        endOfflineLoadGuard();
        if (m_channelButton) m_channelButton->setEnabled(!m_saveAllBinPath.isEmpty());
        if (m_fftBandCombo) m_fftBandCombo->setEnabled(!m_saveAllBinPath.isEmpty());

        if (!result.ok) {
            setHeaderTexts(QStringLiteral("查看保存全部数据"),
                           buildMetaText(result.meta,
                                         QStringList()
                                             << QStringLiteral("当前 bin：%1").arg(QFileInfo(result.binPath).fileName())
                                             << QStringLiteral("读取失败：%1").arg(result.errorMessage)));
            return;
        }

        setPlotVisibleCount(1);
        prepareGraphPlot(m_primaryPlot,
                         QStringLiteral("保存全部相位数据 | %1").arg(QFileInfo(result.binPath).fileName()),
                         result.meta.intervalMeters > 0.0 ? QStringLiteral("距离(m)") : QStringLiteral("通道"),
                         QStringLiteral("时间(hh:mm:ss)"));

        QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
        timeTicker->setTimeFormat(QStringLiteral("%h:%m:%s"));
        m_primaryPlot->yAxis->setTicker(timeTicker);

        QCPColorMap *phaseMap = new QCPColorMap(m_primaryPlot->xAxis, m_primaryPlot->yAxis);
        phaseMap->data()->setSize(result.distanceCells, result.timeCells);
        phaseMap->data()->setRange(QCPRange(result.distanceStart, result.distanceEnd),
                                   QCPRange(0.0, qMax(1e-9, result.durationSeconds)));
        for (int t = 0; t < result.timeCells; ++t) {
            for (int d = 0; d < result.distanceCells; ++d) {
                phaseMap->data()->setCell(d, t, result.waterfallValues[static_cast<qsizetype>(t) * result.distanceCells + d]);
            }
        }

        QCPColorScale *phaseScale = new QCPColorScale(m_primaryPlot);
        phaseScale->setType(QCPAxis::atRight);
        phaseScale->setBarWidth(14);
        phaseScale->setRangeDrag(true);
        phaseScale->setRangeZoom(true);
        phaseScale->axis()->setLabel(QStringLiteral("相位(rad)"));
        QCPLayoutGrid *phaseLayout = qobject_cast<QCPLayoutGrid*>(m_primaryPlot->plotLayout());
        if (phaseLayout) {
            phaseLayout->insertColumn(1);
            const int scaleRow = qMin(phaseLayout->rowCount() - 1, 1);
            phaseLayout->addElement(qMax(0, scaleRow), 1, phaseScale);
        }
        phaseMap->setColorScale(phaseScale);
        phaseMap->setGradient(strainGradient());
        phaseMap->setInterpolate(false);
        phaseMap->setDataRange(QCPRange(-10.0, 10.0));

        QCPMarginGroup *phaseMarginGroup = new QCPMarginGroup(m_primaryPlot);
        m_primaryPlot->axisRect()->setMarginGroup(QCP::msRight, phaseMarginGroup);
        phaseScale->setMarginGroup(QCP::msRight, phaseMarginGroup);
        m_primaryPlot->xAxis->setRange(result.distanceStart, result.distanceEnd);
        m_primaryPlot->yAxis->setRange(0.0, qMax(1e-9, result.durationSeconds));
        m_primaryPlot->yAxis->setRangeReversed(false);
        m_primaryPlot->replot(QCustomPlot::rpQueuedReplot);

        QStringList phaseExtraLines;
        phaseExtraLines << QStringLiteral("当前 bin：%1 | 完整帧数=%2 | 时长=%3")
                               .arg(QFileInfo(result.binPath).fileName())
                               .arg(result.frameCount)
                               .arg(formatSeconds(result.durationSeconds));
        phaseExtraLines << QStringLiteral("显示：X轴=距离 | Y轴=时间 | 频段=%1-%2 Hz 带通滤波 | 时间抽样=%3点 | 距离抽样=%4行")
                               .arg(result.bandLowerHz, 0, 'g', 6)
                               .arg(result.bandUpperHz, 0, 'g', 6)
                               .arg(result.frameStep)
                               .arg(result.rowStep);
        setHeaderTexts(QStringLiteral("查看保存全部数据"),
                       buildMetaText(result.meta, phaseExtraLines));
        return;

        prepareGraphPlot(m_primaryPlot,
                         QStringLiteral("保存全部数据瀑布图 | %1").arg(QFileInfo(result.binPath).fileName()),
                         QStringLiteral("时间(hh:mm:ss)"),
                         result.meta.intervalMeters > 0.0 ? QStringLiteral("距离(m)") : QStringLiteral("通道"));

        QSharedPointer<QCPAxisTickerTime> waterfallTicker(new QCPAxisTickerTime);
        waterfallTicker->setTimeFormat(QStringLiteral("%h:%m:%s"));
        m_primaryPlot->xAxis->setTicker(waterfallTicker);

        QCPColorMap *colorMap = new QCPColorMap(m_primaryPlot->xAxis, m_primaryPlot->yAxis);
        colorMap->data()->setSize(result.timeCells, result.distanceCells);
        colorMap->data()->setRange(QCPRange(0.0, qMax(1e-9, result.durationSeconds)),
                                   QCPRange(result.distanceStart, result.distanceEnd));
        for (int x = 0; x < result.timeCells; ++x) {
            for (int y = 0; y < result.distanceCells; ++y) {
                colorMap->data()->setCell(x, y, result.waterfallValues[static_cast<qsizetype>(x) * result.distanceCells + y]);
            }
        }

        QCPColorScale *colorScale = new QCPColorScale(m_primaryPlot);
        colorScale->setType(QCPAxis::atRight);
        colorScale->setBarWidth(14);
        colorScale->setRangeDrag(true);
        colorScale->setRangeZoom(true);
        colorScale->axis()->setLabel(QStringLiteral("相位(rad)"));
        m_primaryPlot->plotLayout()->insertColumn(1);
        m_primaryPlot->plotLayout()->addElement(1, 1, colorScale);
        colorMap->setColorScale(colorScale);
        colorMap->setGradient(strainGradient());
        colorMap->setInterpolate(false);
        colorMap->setDataRange(QCPRange(-10.0, 10.0));

        QCPMarginGroup *marginGroup = new QCPMarginGroup(m_primaryPlot);
        m_primaryPlot->axisRect()->setMarginGroup(QCP::msRight, marginGroup);
        colorScale->setMarginGroup(QCP::msRight, marginGroup);
        m_primaryPlot->xAxis->setRange(0.0, qMax(1e-9, result.durationSeconds));
        m_primaryPlot->yAxis->setRange(result.distanceStart, result.distanceEnd);
        m_primaryPlot->yAxis->setRangeReversed(true);
        m_primaryPlot->replot(QCustomPlot::rpQueuedReplot);

        const ChannelAnalysisResult channel = result.channel;
        const int displayedChannel = channelValueFromRow(result.meta, channel.channelRow);
        prepareGraphPlot(m_secondaryPlot,
                         QStringLiteral("通道 %1 波形").arg(displayedChannel),
                         QStringLiteral("时间(hh:mm:ss)"),
                         QStringLiteral("相位(rad)"));
        m_secondaryPlot->addGraph();
        styleSmoothWaveform(m_secondaryPlot, m_secondaryPlot->graph(0), QColor(40, 110, 255));
        QSharedPointer<QCPAxisTickerTime> waveTicker(new QCPAxisTickerTime);
        waveTicker->setTimeFormat(QStringLiteral("%h:%m:%s"));
        m_secondaryPlot->xAxis->setTicker(waveTicker);
        m_secondaryPlot->graph(0)->setData(channel.waveformKeys, channel.waveformValues, true);
        m_secondaryPlot->xAxis->setRange(0.0, qMax(1.0, static_cast<double>(channel.totalSamples) / result.meta.frequency));
        const double wavePad = qMax(0.08, (channel.waveformMax - channel.waveformMin) * 0.15);
        m_secondaryPlot->yAxis->setRange(channel.waveformMin - wavePad, channel.waveformMax + wavePad);
        m_secondaryPlot->replot(QCustomPlot::rpQueuedReplot);

        prepareGraphPlot(m_tertiaryPlot,
                         QStringLiteral("通道 %1 FFT").arg(displayedChannel),
                         QStringLiteral("频率(Hz)"),
                         QStringLiteral("幅值(dB)"));
        m_tertiaryPlot->addGraph();
        m_tertiaryPlot->graph(0)->setPen(QPen(QColor(255, 142, 54), 1.1));
        m_tertiaryPlot->graph(0)->setAdaptiveSampling(true);
        m_tertiaryPlot->graph(0)->setAntialiased(false);
        m_currentFftKeys = channel.fftKeys;
        m_currentFftValues = channel.fftValues;
        m_currentFftMinDb = channel.fftMin;
        m_currentFftMaxDb = channel.fftMax;
        m_tertiaryPlot->graph(0)->setData(m_currentFftKeys, m_currentFftValues, true);
        updateFftBandRange();
        m_tertiaryPlot->replot(QCustomPlot::rpQueuedReplot);

        QStringList extraLines;
        extraLines << QStringLiteral("当前 bin：%1 | 完整帧数=%2 | 时长=%3")
                          .arg(QFileInfo(result.binPath).fileName())
                          .arg(result.frameCount)
                          .arg(formatSeconds(result.durationSeconds));
        if (result.ignoredTailBytes > 0) {
            extraLines << QStringLiteral("提示：文件尾部有 %1 字节不完整帧，已忽略。").arg(result.ignoredTailBytes);
        }
        extraLines << QStringLiteral("显示：瀑布图时间抽样=%1点 | 距离抽样=%2行 | 通道波形抽样=%3点 | FFT样本=%4")
                          .arg(result.frameStep)
                          .arg(result.rowStep)
                          .arg(channel.plotStep)
                          .arg(channel.fftSampleCount);
        setHeaderTexts(QStringLiteral("查看保存全部数据"),
                       buildMetaText(result.meta, extraLines));
    });
    watcher->setFuture(future);
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::startSaveAllChannelLoad(int channelRow)
{
    if (!m_workerPool || m_saveAllBinPath.isEmpty() || m_saveAllFolderPath.isEmpty()) return;
    beginOfflineLoadGuard();
    if (m_channelButton) m_channelButton->setEnabled(false);

    SavedFolderMeta meta = parseSavedFolderMeta(m_saveAllFolderPath);
    const QString binPath = m_saveAllBinPath;
    const auto future = QtConcurrent::run(m_workerPool, [binPath, meta, channelRow]() {
        return readChannelAnalysis(binPath, meta, channelRow);
    });

    auto *watcher = new QFutureWatcher<ChannelAnalysisResult>(this);
    connect(watcher, &QFutureWatcher<ChannelAnalysisResult>::finished, this, [this, watcher, meta]() {
        const ChannelAnalysisResult channel = watcher->result();
        watcher->deleteLater();
        endOfflineLoadGuard();
        if (m_channelButton) m_channelButton->setEnabled(!m_saveAllBinPath.isEmpty());
        const SavedFolderMeta effectiveMeta = effectiveMetaForBin(meta, QFileInfo(channel.binPath).size());
        if (!channel.ok) {
            setHeaderTexts(QStringLiteral("查看保存全部数据"),
                           buildMetaText(effectiveMeta,
                                         QStringList()
                                             << QStringLiteral("当前 bin：%1").arg(QFileInfo(channel.binPath).fileName())
                                             << QStringLiteral("通道读取失败：%1").arg(channel.errorMessage)));
            return;
        }

        const int displayedChannel = channelValueFromRow(effectiveMeta, channel.channelRow);
        prepareGraphPlot(m_secondaryPlot,
                         QStringLiteral("通道 %1 波形").arg(displayedChannel),
                         QStringLiteral("时间(hh:mm:ss)"),
                         QStringLiteral("相位(rad)"));
        m_secondaryPlot->addGraph();
        styleSmoothWaveform(m_secondaryPlot, m_secondaryPlot->graph(0), QColor(40, 110, 255));
        QSharedPointer<QCPAxisTickerTime> waveTicker(new QCPAxisTickerTime);
        waveTicker->setTimeFormat(QStringLiteral("%h:%m:%s"));
        m_secondaryPlot->xAxis->setTicker(waveTicker);
        m_secondaryPlot->graph(0)->setData(channel.waveformKeys, channel.waveformValues, true);
        m_secondaryPlot->xAxis->setRange(0.0, qMax(1.0, static_cast<double>(channel.totalSamples) / effectiveMeta.frequency));
        const double wavePad = qMax(0.08, (channel.waveformMax - channel.waveformMin) * 0.15);
        m_secondaryPlot->yAxis->setRange(channel.waveformMin - wavePad, channel.waveformMax + wavePad);
        m_secondaryPlot->replot(QCustomPlot::rpQueuedReplot);

        prepareGraphPlot(m_tertiaryPlot,
                         QStringLiteral("通道 %1 FFT").arg(displayedChannel),
                         QStringLiteral("频率(Hz)"),
                         QStringLiteral("幅值(dB)"));
        m_tertiaryPlot->addGraph();
        m_tertiaryPlot->graph(0)->setPen(QPen(QColor(255, 142, 54), 1.1));
        m_tertiaryPlot->graph(0)->setAdaptiveSampling(true);
        m_tertiaryPlot->graph(0)->setAntialiased(false);
        m_currentFftKeys = channel.fftKeys;
        m_currentFftValues = channel.fftValues;
        m_currentFftMinDb = channel.fftMin;
        m_currentFftMaxDb = channel.fftMax;
        m_tertiaryPlot->graph(0)->setData(m_currentFftKeys, m_currentFftValues, true);
        updateFftBandRange();
        m_tertiaryPlot->replot(QCustomPlot::rpQueuedReplot);

        setHeaderTexts(QStringLiteral("查看保存全部数据"),
                       buildMetaText(effectiveMeta,
                                     QStringList()
                                         << QStringLiteral("当前 bin：%1 | 当前通道=%2")
                                                .arg(QFileInfo(channel.binPath).fileName())
                                                .arg(displayedChannel)
                                         << QStringLiteral("通道波形抽样步长=%1 | FFT样本=%2")
                                                .arg(channel.plotStep)
                                                .arg(channel.fftSampleCount)));
    });
    watcher->setFuture(future);
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::updateFftBandRange()
{
    if (!m_tertiaryPlot || !m_fftBandCombo) return;

    double lower = 0.001;
    double upper = 1.0;
    switch (m_fftBandCombo->currentIndex()) {
    case 1:
        lower = 1.0;
        upper = 10.0;
        break;
    case 2:
        lower = 10.0;
        upper = 100.0;
        break;
    default:
        lower = 0.001;
        upper = 1.0;
        break;
    }

    m_tertiaryPlot->xAxis->setRange(lower, upper);

    if (!m_currentFftKeys.isEmpty() && m_currentFftKeys.size() == m_currentFftValues.size()) {
        bool found = false;
        double minDb = std::numeric_limits<double>::max();
        double maxDb = -std::numeric_limits<double>::max();
        for (int i = 0; i < m_currentFftKeys.size(); ++i) {
            if (m_currentFftKeys[i] < lower || m_currentFftKeys[i] > upper) continue;
            const double value = m_currentFftValues[i];
            if (value < minDb) minDb = value;
            if (value > maxDb) maxDb = value;
            found = true;
        }
        if (!found) {
            minDb = m_currentFftMinDb;
            maxDb = m_currentFftMaxDb;
        }
        if (qFuzzyCompare(minDb, maxDb)) {
            minDb -= 3.0;
            maxDb += 3.0;
        }
        const double pad = qMax(2.0, (maxDb - minDb) * 0.12);
        m_tertiaryPlot->yAxis->setRange(minDb - pad, maxDb + pad);
    }
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::onSaveAllChannelRequested()
{
    startSaveAllChannelLoad(selectedSaveAllRow());
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于离线数据加载与浏览层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void SavedDataViewer::onFftBandChanged(int index)
{
    Q_UNUSED(index);
    if (!m_saveAllBinPath.isEmpty() && !m_saveAllLoading) {
        startSaveAllBinLoad(m_saveAllBinPath, selectedSaveAllRow());
        return;
    }
    updateFftBandRange();
    if (m_tertiaryPlot) m_tertiaryPlot->replot(QCustomPlot::rpQueuedReplot);
}
