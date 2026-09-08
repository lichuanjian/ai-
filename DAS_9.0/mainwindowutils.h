#ifndef MAINWINDOWUTILS_H
#define MAINWINDOWUTILS_H

#include <QWidget>
#include <QString>
#include <QLabel>
#include "qcustomplot.h"

class MainWindowUtils
{
public:
    /** @brief 在性能标签中显示耗时，并按一帧预算将超时项标为红色。 */
    static void updateTimeLabel(QLabel *label, double timeMs, int frequency);
    /** @brief 从“1km/5km/10km/30km/50km”等界面文本提取末端距离（km）。 */
    static int getEndIntBySelectedText(const QString& selectedText);
    /** @brief 根据有效行数、抽取率和频率计算图表 X 轴最大距离。 */
    static int calculateMaxX(int realRows, int extractCount, int frequency);

    /** @brief 返回采样频率对应的原始空间点距（m）。 */
    static double meterPerRawPoint(int frequency);
    /** @brief 返回考虑空间抽取后的相邻输出通道间距（m）。 */
    static double meterPerExtractedChannel(int frequency, int extractCount);
    /** @brief 将零基通道索引换算为距起点的物理距离（m）。 */
    static double channelToMeter(int channelIndex, int frequency, int extractCount);

    /** @brief 统一配置 QCustomPlot 的白色背景、坐标轴、网格和图例视觉风格。 */
    static void setPlotBackground(QCustomPlot *plot);
};

#endif // MAINWINDOWUTILS_H
