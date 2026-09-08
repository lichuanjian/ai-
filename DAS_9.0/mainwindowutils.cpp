/**
 * @file mainwindowutils.cpp
 * @brief 主窗口的单位换算、性能提示和图表风格共用函数。
 *
 * 本文件集中定义不同采样率的原始空间点距、抽取后距离映射、耗时颜色阈值及 QCustomPlot 的统一视觉配置。
 */
#include "MainWindowUtils.h"
#include <QDebug>
#include <QtMath>

void MainWindowUtils::updateTimeLabel(QLabel *label, double timeMs, int frequency)
{
    if (!label) {
        qWarning() << "Time label is null!";
        return;
    }

    const float threshold = (5000.0f / frequency) * 1000.0f;
    const QString text = QString("%1 ms").arg(timeMs);
    if (label->text() != text) {
        label->setText(text);
    }

    const QString style = timeMs > threshold
                              ? QStringLiteral("color: #d95757; font-weight: 700;")
                              : QStringLiteral("color: #1f6fd6; font-weight: 600;");
    if (label->styleSheet() != style) {
        label->setStyleSheet(style);
    }
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口通用界面工具层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindowUtils::getEndIntBySelectedText(const QString& selectedText)
{
    const QString normalized = selectedText.trimmed().toLower().remove(' ');
    if (normalized.contains("50km")) return 50;
    if (normalized.contains("30km")) return 30;
    if (normalized.contains("10km")) return 10;
    if (normalized.contains("5km")) return 5;
    if (normalized.contains("1km")) return 1;
    return 0;
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口通用界面工具层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
double MainWindowUtils::meterPerRawPoint(int frequency)
{
    if (frequency == 2000) return 2.0;
    if (frequency == 3333) return 1.2;
    return 0.4;
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口通用界面工具层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
double MainWindowUtils::meterPerExtractedChannel(int frequency, int extractCount)
{
    const int safeExtract = qMax(1, extractCount);
    return meterPerRawPoint(frequency) * safeExtract;
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口通用界面工具层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
double MainWindowUtils::channelToMeter(int channelIndex, int frequency, int extractCount)
{
    const int safeChannel = qMax(0, channelIndex);
    return safeChannel * meterPerExtractedChannel(frequency, extractCount);
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口通用界面工具层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindowUtils::calculateMaxX(int realRows, int extractCount, int frequency)
{
    const int safeRows = qMax(0, realRows);
    const double maxMeter = safeRows * meterPerExtractedChannel(frequency, extractCount);
    return static_cast<int>(qRound(maxMeter));
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口通用界面工具层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowUtils::setPlotBackground(QCustomPlot *plot)
{
    if (!plot) return;

    const QColor axisColor(108, 130, 152);
    const QColor labelColor(52, 72, 92);
    const QColor gridColor(201, 214, 228, 150);
    const QColor subGridColor(228, 236, 244, 190);

    plot->setBackground(QColor(255, 255, 255));
    plot->axisRect()->setBackground(QBrush(QColor(255, 255, 255)));
    plot->axisRect()->setAutoMargins(QCP::msAll);
    plot->axisRect()->setMargins(QMargins(6, 6, 6, 8));

    const QList<QCPAxis*> axes = {plot->xAxis, plot->yAxis, plot->xAxis2, plot->yAxis2};
    for (QCPAxis *axis : axes) {
        if (!axis) continue;
        axis->setBasePen(QPen(axisColor, 1));
        axis->setTickPen(QPen(axisColor, 1));
        axis->setSubTickPen(QPen(axisColor, 1));
        axis->setTickLabelColor(labelColor);
        axis->setLabelColor(labelColor);
        axis->grid()->setPen(QPen(gridColor, 1, Qt::DashLine));
        axis->grid()->setSubGridPen(QPen(subGridColor, 1, Qt::DotLine));
        axis->grid()->setSubGridVisible(true);
        axis->grid()->setZeroLinePen(QPen(QColor(179, 197, 218, 180), 1));
    }

    if (plot->legend) {
        plot->legend->setBorderPen(QPen(QColor(191, 205, 221)));
        plot->legend->setBrush(QBrush(QColor(255, 255, 255, 230)));
        plot->legend->setTextColor(QColor(48, 66, 86));
    }
}
