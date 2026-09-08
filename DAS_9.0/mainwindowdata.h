#ifndef MAINWINDOWDATA_H
#define MAINWINDOWDATA_H
#include <QObject>
#include <QTimer>
#include <QQueue>
#include <deque>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QVector>
#include "qcustomplot.h"

class MainWindowData : public QObject
{
    Q_OBJECT
public:
    /** @brief 创建主窗口数据渲染器及轻量音频刷新定时器。 */
    explicit MainWindowData(QObject *parent = nullptr);

    /** @brief 兼容单音频图接口；内部转换为一个图的列表。 */
    void setAudioPlot(QCustomPlot *plot);
    /** @brief 设置全部实时音频图，并同步待渲染队列数量。 */
    void setAudioPlots(const QVector<QCustomPlot*>& plots);
    /** @brief 设置 RMS/应变显示的目标图。 */
    void setRMSWaterPlot(QCustomPlot *plot);
    /** @brief 设置 FFT 显示的目标图。 */
    void setFFTWaterPlot(QCustomPlot *plot);
    /** @brief 开关指定音频图的可视区 Y 轴自动缩放。 */
    void setAudioYAxisAutoScale(int channelIndex, bool enabled);

    /** @brief 更新后续 RMS/应变帧的频率元数据。 */
    void setFrequency(int freq) { m_rmsFrequency = freq; }
    /** @brief 更新后续 RMS/应变帧的空间行数。 */
    void setRows(int rows) { m_rmsRows = rows; }
    /** @brief 更新后续 RMS/应变帧的空间抽取率。 */
    void setExtractCount(int count) { m_rmsExtractCount = count; }
    /** @brief 切换 FFT 的瀑布图与单帧曲线显示模式。 */
    void setFftLineMode(bool enabled) { m_fftLineMode = enabled; }
    /** @brief 指定输入是否为应变值；退出应变入口时同时清除当前应变显示状态。 */
    void setStrainMode(bool enabled) { m_strainValueMode = enabled; if (!enabled) m_isStrainMode = false; }
    /** @brief 切换 RMS/应变的瀑布图与定位曲线显示模式，并强制坐标轴刷新。 */
    void setRmsLineMode(bool enabled) { m_rmsLineMode = enabled; m_forceRmsAxisRefresh = true; }
    /** @brief 暂停或恢复 RMS/应变图的实时重绘，数据缓存仍会保留最新帧。 */
    void setRmsPaused(bool paused) { m_rmsPaused = paused; }
    /** @brief 切换 RMS 曲线横轴为通道或物理距离。 */
    void setRmsXAxisChannelMode(bool enabled)
    {
        if (m_rmsXAxisChannelMode == enabled) return;
        m_rmsXAxisChannelMode = enabled;
        m_forceRmsAxisRefresh = true;
    }
    /** @brief 返回 RMS 曲线当前是否以通道号作为横轴。 */
    bool rmsXAxisChannelMode() const { return m_rmsXAxisChannelMode; }
    /** @brief 清空所有音频图时间基准与缓存队列。 */
    void resetAudioTimeline();
    /** @brief 清空指定音频通道的待显示数据和曲线。 */
    void clearAudioChannelData(int channelIndex);
    /** @brief 使用最近缓存的 RMS/应变帧重新绘制，供模式切换时调用。 */
    void refreshRmsPlotFromCache();
    /** @brief 停止定时渲染并断开图表指针，防止关闭窗口后的异步访问。 */
    void shutdown();

signals:
    /** @brief 预留的渲染完成通知，供未来需要帧同步的调用方连接。 */
    void renderFinished();

public slots:
    /** @brief 接收多通道音频数据，将其附加到各通道的待绘制队列。 */
    void onReceiveMultiAudioData(const QList<QVector<float>>& rowsData, int len, int frequency);
    /** @brief 接收 RMS 帧、更新缓存并按当前模式绘制 RMS 定位图。 */
    void onReceiveRMSData(const QList<QVector<float>>& data, int rows, int cols, int frequency, int extractCount);
    /** @brief 接收未滤波支路的应变/相位帧，并按应变显示规则渲染。 */
    void  onReceiveStrainData(const QList<QVector<float>>& data, int rows, int cols, int frequency, int extractCount);
    /** @brief 接收单边幅度谱并渲染 FFT 曲线或频谱瀑布图。 */
    void onReceiveFFTData(QVector<float> data, int length, int frequency);
    /** @brief 定时消耗音频队列并以受限预算刷新各音频图。 */
    void onAudioRenderTick();

    // 删除：RMS 定时器渲染槽函数
    // void renderRMSWaterPlot();

    // 删除：定时器控制函数（无定时器需要控制）
    // void startRenderTimers();
    // void stopRenderTimers();

private:
    // 图表对象
    QCustomPlot *m_audioPlot = nullptr;
    QVector<QCustomPlot*> m_audioPlots;
    QCustomPlot *m_rmsWaterPlot = nullptr;
    QCustomPlot *m_fftWaterPlot = nullptr;

    // 删除：所有定时器
    // QTimer *m_rmsTimer = nullptr;
    // QTimer *m_audioTimer = nullptr;

    // 删除：RMS 数据队列
    // QQueue<QList<float>> m_rmsDataQueue;

    // 删除：状态变量
    // bool m_audioFlag = true;
    // bool m_rmsFlag = true;
    int m_rmsFrequency = 10000;
    int m_rmsRows = 1024*23/8;
    int m_rmsExtractCount = 8;
    int m_fftFrequency = 10000;
    bool m_isStrainMode = false;
    bool m_strainValueMode = false;
    bool m_fftLineMode = false;
    bool m_fftAxisLockedByUser = false;
    bool m_fftApplyingAxisUpdate = false;
    bool m_rmsLineMode = false;
    bool m_rmsPaused = false;
    bool m_rmsXAxisChannelMode = false;
    bool m_forceRmsAxisRefresh = true;
    bool m_hasLatestRmsFrame = false;
    bool m_latestRmsFrameIsStrain = false;
    QMetaObject::Connection m_fftXAxisRangeConn;
    QMetaObject::Connection m_fftYAxisRangeConn;
    QElapsedTimer m_audioReplotLimiter;
    QElapsedTimer m_rmsReplotLimiter;
    QElapsedTimer m_fftReplotLimiter;
    QTimer *m_audioRenderTimer = nullptr;
    QVector<std::deque<float>> m_audioPendingBuffers;
    QVector<bool> m_audioAutoScaleEnabled;
    int m_audioPendingSampleRate = 10000;
    bool m_shuttingDown = false;

    // 音频时间轴累计值（用于连续渲染）
    double m_audioTimeKey = 0.0;
    QVector<double> m_audioTimeKeys;
    QVector<float> m_latestRmsFrame;
    double m_opticalWavelength = 1550e-9; // meter
    double m_groupIndex = 1.4682;
    double m_photoElasticFactor = 0.78;

    /** @brief 将一段音频数据追加到曲线并维护连续时间轴。 */
    void renderAudioToPlot(int channelIndex, QCustomPlot* plot, double& timeKey, const QVector<float>& xdata, int len, int frequency);
    /** @brief 按距离或通道模式构造 RMS/应变曲线的横轴坐标。 */
    QVector<double> buildRmsXAxis(int pointCount, int frequency, int extractCount) const;
    /** @brief 渲染最近 RMS/应变帧为定位曲线。 */
    void renderRmsLineFrame(const QVector<float>& frame, bool strainFrame, int rows, int frequency, int extractCount);
    /** @brief 根据光学波长、群折射率和光弹系数返回相位到微应变的换算比例。 */
    double getPhaseToMicroStrainScale() const;
    /** @brief 依据最小刷新间隔判断是否允许重绘，限制高频数据造成的 GUI 压力。 */
    bool shouldReplot(QElapsedTimer& timer, qint64 minIntervalMs);
    /** @brief 对原始音频相位执行显示单位变换。 */
    double transformAudioValue(float rawValue) const;
    /** @brief 计算当前可见 X 范围内曲线的有效 Y 极值。 */
    bool findAudioVisibleYRange(QCustomPlot* plot, double *minValue, double *maxValue) const;
    /** @brief 基于可见数据更新音频图 Y 轴，避免空白或异常放大。 */
    void updateAudioYAxisToVisibleData(QCustomPlot* plot, bool forceUpdate);
    /** @brief 根据积压量和采样率决定单次 tick 消耗的样本预算。 */
    int computeAudioRenderBudget(const std::deque<float>& pendingBuffer, int sampleRate) const;
    /** @brief 扩缩待渲染队列，使其与音频图数量一致。 */
    void ensureAudioPendingBufferCount();
};

#endif // MAINWINDOWDATA_H
