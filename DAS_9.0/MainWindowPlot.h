#ifndef MAINWINDOWPLOT_H
#define MAINWINDOWPLOT_H
#include <QObject>
#include "qcustomplot.h"
#include "MainWindowUtils.h"

class MainWindowPlot : public QObject
{
    Q_OBJECT
public:
    /** @brief 创建图表初始化器；该对象不拥有传入的 QCustomPlot。 */
    explicit MainWindowPlot(QObject *parent = nullptr);

    /** @brief 初始化单通道音频/相位时域曲线及时间刻度。 */
    void initAudioPlot(QCustomPlot *plot, const QString& title = QStringLiteral("音频图"));
    /** @brief 初始化 RMS 定位瀑布图，包括色图、色标和基于距离的坐标轴。 */
    void initRMSWaterPlot(QCustomPlot *plot, int realRows, int extractCount, int frequency);
    /**
     * @brief 初始化单帧 RMS 或应变定位曲线。
     * @param plot 需要重建的目标图表。
     * @param realRows 当前有效空间行数。
     * @param extractCount 空间抽取率，用于距离轴标定。
     * @param frequency 采样频率，用于距离标定。
     * @param strainMode true 使用应变标题、单位和曲线颜色。
     * @param channelXAxisMode true 使用通道号而非物理距离作为横轴。
     */
    void initRMSLinePlot(QCustomPlot *plot,
                         int realRows,
                         int extractCount,
                         int frequency,
                         bool strainMode,
                         bool channelXAxisMode);
    /** @brief 初始化按时间累积的频谱瀑布图。 */
    void initFFTWaterPlot(QCustomPlot *plot, int frequency);
    /** @brief 初始化单帧 FFT 幅度曲线。 */
    void initFFTSpectrumPlot(QCustomPlot *plot, int frequency);
    /** @brief 初始化光强色图。 */
    void initIntensityPlot(QCustomPlot *plot);

    /** @brief 将已有 RMS 瀑布图切换为应变标题、单位、色标和数据范围。 */
    void RMStoStrainWaterPlot(QCustomPlot *plot,int realRows, int extractCount, int frequency);
    /** @brief 将已有应变瀑布图恢复为 RMS 标题、单位、色标和数据范围。 */
    void StrainToRMSWaterPlot(QCustomPlot *plot,int realRows, int extractCount, int frequency);
    /** @brief 返回 RMS/频谱使用的低值蓝色至高值红色通用渐变。 */
    QCPColorGradient getDefaultGradient();
    /** @brief 返回以零值白色为中心的负蓝正红应变渐变。 */
    QCPColorGradient getStrainGradient();
    /** @brief 返回用于光强显示的灰阶渐变。 */
    QCPColorGradient getIntensityGradient();
private:
    /** @brief 清理旧标题并将新标题作为布局顶部的 QCPTextElement 插入。 */
    void setPlotTitle(QCustomPlot *plot, const QString& title);
};

#endif // MAINWINDOWPLOT_H
