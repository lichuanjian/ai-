#ifndef FILTER_H
#define FILTER_H

#include <QAudioSink>
#include <QObject>
#include <QVector>
#include <QList>
#include <QtGlobal>

#include "ParameterManager.h"
#include "receive_data.h"
#include "Iir.h"

// 前向声明（减少头文件依赖，提升编译速度）
QT_FORWARD_DECLARE_CLASS(QAudioDevice)
QT_FORWARD_DECLARE_CLASS(QAudioInput)
QT_FORWARD_DECLARE_CLASS(QAudioSource)
QT_FORWARD_DECLARE_CLASS(QChart)
QT_FORWARD_DECLARE_CLASS(QLineSeries)

// 滤波处理类：实现基于切比雪夫I型8阶高通滤波器的并行数据滤波
// 功能：并行滤波、音频行数据提取、列方向数据提取、参数动态更新
class Filter : public QObject {
    Q_OBJECT
public:
    /**
     * @brief 创建滤波与分发模块，并从 ParameterManager 读取初始监听行和截止频率。
     * @param parent Qt 父对象；通常为空，随后由 main.cpp 移动到独立滤波线程。
     */
    explicit Filter(QObject *parent = nullptr);
    /** @brief 删除每行独立的 IIR 实例，避免滤波器银行泄漏。 */
    ~Filter() override;

    int m_frequency = 100; // 当前数据采样频率（用于透传给下游）
    /**
     * @brief 响应监听通道或高通截止频率等运行参数。
     *
     * 截止频率变化会标记滤波器银行需要重建；监听通道变化只更新后续行提取位置。
     */
    void onParameterChanged(const QString& key, const QVariant& value);
    /** @brief 历史兼容的记忆式解包预留接口；当前未实现且不在实时链中调用。 */
    void array_unwrap_jiyi(float array[], int size);
    /** @brief 历史兼容的无记忆解包预留接口；当前未实现且不在实时链中调用。 */
    void array_unwrap(float array[], int size);

signals:
    /**
     * @brief 发送第一路监听行，供 Audio 与 FFT_Calculate 并行消费。
     * @param xdata 已按当前滤波开关处理的单行时域样本。
     * @param len 有效样本数。
     * @param frequency 样本采样率（Hz），不是滤波截止频率。
     */
    void array_tongguolvboSignal(const QVector<float>& xdata, int len, int frequency);

    /**
     * @brief 发送两路监听行及其空间索引，供主窗口实时音频图显示。
     * @param rowsData 每个元素对应一条监听行的时域样本。
     * @param channelRows 与 rowsData 同序的零基空间行索引。
     * @param len 每行有效样本数。
     * @param frequency 采样率。
     * @param extractCount 当前空间抽取率，供 UI 换算监听距离。
     */
    void multiAudioRowsSignal(const QList<QVector<float>>& rowsData,
                              const QVector<int>& channelRows,
                              int len,
                              int frequency,
                              int extractCount);

    /**
     * @brief 在滤波开启时发送原地处理完成的整帧矩阵给 RMS 模块。
     * @warning array 指向上游复用的工作缓冲；接收者应仅在本次槽调用期间读取。
     */
    void Filter_my_array_Signal(float* array, int rows, int cols, int frequency, int extractCount);

    /** @brief 上报当前帧滤波、监听行提取和分发的总耗时（毫秒）。 */
    void processingTimeMeasured(qint64 elapsedTime, int frequency);

    /**
     * @brief 在滤波关闭时发送均匀抽取的五个时间列，用于应变/相位显示支路。
     * @param data 外层为选取列，内层为所有空间行的值。
     */
    void Data_after_time_Extraction(const QList<QVector<float>>& data,
                                    int rows,
                                    int cols,
                                    int frequency,
                                    int extractCount);

public slots:
    /**
     * @brief 处理一帧解缠矩阵：可选高通、提取双监听行，并向 RMS/音频/FFT/应变支路分发。
     * @param array 行主序可写矩阵；启用滤波时会被原地替换为滤波结果。
     * @param rows 空间行数，cols 每行时间样本数。
     * @param frequency 样本采样率。
     * @param extractCount 当前空间抽取率。
     */
    void array_handleSignal(float* array, int rows, int cols, int frequency, int extractCount);

    /** @brief 切换高通滤波；从关闭恢复为开启时重置每行 IIR 状态。 */
    void Receive_is_filter(bool ok);

private:
    QVector<Iir::ChebyshevI::HighPass<8>*> m_rowFilters; // 每行独立的8阶高通滤波器数组
    int m_lastRows = 0;         // 上次创建滤波器时的行数（用于判断是否重建）
    bool m_isFilterEnabled = true; // 滤波功能使能标志
    int m_audioLocation = 0;    // 主音频行位置
    QVector<int> m_audioLocations = {200, 300}; // 双音频行位置（备用通道）
    double m_cutoffHz = 20;    // 高通截止频率，默认 1Hz
    bool m_filterBankDirty = true; // 参数变化后标记重建滤波器

    /**
     * @brief 按当前行数、采样率和截止频率创建每行独立的八阶高通滤波器。
     * @note 会先释放旧银行，并夹紧截止频率使其严格低于 Nyquist。
     */
    void createRowFilters(int rows, int frequency);
    /** @brief 删除并清空所有行滤波器，同时重置记录的行数。 */
    void clearRowFilters();
};

#endif // FILTER_H
