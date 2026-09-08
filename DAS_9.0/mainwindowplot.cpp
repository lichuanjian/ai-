/**
 * @file mainwindowplot.cpp
 * @brief QCustomPlot 图表结构、色标、坐标轴与交互方式的初始化实现。
 *
 * MainWindowPlot 只负责“图应该长什么样”，不持有数据也不做实时计算；MainWindowData 负责将实时
 * RMS、应变、音频和 FFT 数据填入这里创建的曲线或色图。初始化路径会先清理旧布局，避免切换模式时重复色标。
 */
#include "MainWindowPlot.h"

MainWindowPlot::MainWindowPlot(QObject *parent) : QObject(parent) {}
static void clearColorMapCells(QCPColorMap *map)
{
    if (!map || !map->data()) return;
    map->data()->fill(0.0);
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
static void configureAxisZoomDrag(QCustomPlot *plot)
{
    if (!plot || !plot->axisRect()) return;
    QCPAxisRect *rect = plot->axisRect();
    rect->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    rect->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    rect->setRangeDragAxes(plot->xAxis, plot->yAxis);
    rect->setRangeZoomAxes(plot->xAxis, plot->yAxis);
}

/**
 * @brief 清空或复位当前功能相关的状态，确保后续流程从一致状态继续。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
static void clearPlotLayoutDecorations(QCustomPlot *plot)
{
    if (!plot) return;
    QCPLayoutGrid *layoutGrid = qobject_cast<QCPLayoutGrid*>(plot->plotLayout());
    if (!layoutGrid) return;

    for (int i = layoutGrid->elementCount() - 1; i >= 0; --i) {
        QCPLayoutElement *elem = layoutGrid->elementAt(i);
        if (qobject_cast<QCPTextElement*>(elem) || qobject_cast<QCPColorScale*>(elem)) {
            layoutGrid->takeAt(i);
            delete elem;
        }
    }
    layoutGrid->simplify();
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
static QColor plotTextColor()
{
    return QColor(17, 17, 17);
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
static QColor plotAxisColor()
{
    return QColor(95, 110, 128);
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
static QPen audioWaveformPen()
{
    QPen pen(QColor(255, 128, 0), 1.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
static void configureAudioWaveformStyle(QCustomPlot *plot)
{
    if (!plot || !plot->axisRect()) return;

    MainWindowUtils::setPlotBackground(plot);
    plot->setBackground(QColor(255, 255, 255));
    plot->axisRect()->setBackground(QBrush(QColor(255, 255, 255)));

    const QColor axisColor(96, 123, 150);
    const QColor labelColor(47, 66, 86);
    const QColor gridColor(148, 172, 196, 170);
    const QColor subGridColor(211, 224, 237, 175);
    const QColor zeroLineColor(120, 150, 180, 180);

    const QList<QCPAxis*> axes = {plot->xAxis, plot->yAxis, plot->xAxis2, plot->yAxis2};
    for (QCPAxis *axis : axes) {
        if (!axis) continue;
        axis->setSubTicks(true);
        axis->setBasePen(QPen(axisColor, 1));
        axis->setTickPen(QPen(axisColor, 1));
        axis->setSubTickPen(QPen(axisColor, 1));
        axis->setTickLabelColor(labelColor);
        axis->setLabelColor(labelColor);
        axis->grid()->setPen(QPen(gridColor, 1, Qt::DashLine));
        axis->grid()->setSubGridPen(QPen(subGridColor, 1, Qt::DotLine));
        axis->grid()->setSubGridVisible(true);
        axis->grid()->setZeroLinePen(QPen(zeroLineColor, 1, Qt::DashLine));
    }
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::setPlotTitle(QCustomPlot *plot, const QString& title)
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

    if (title.trimmed().isEmpty()) {
        if (firstTitle) {
            for (int i = 0; i < layoutGrid->elementCount(); ++i) {
                if (layoutGrid->elementAt(i) == firstTitle) {
                    layoutGrid->takeAt(i);
                    break;
                }
            }
            delete firstTitle;
            layoutGrid->simplify();
        }
        return;
    }

    if (!firstTitle) {
        firstTitle = new QCPTextElement(plot);
        firstTitle->setFont(QFont("Microsoft YaHei UI", 13, QFont::DemiBold));
        layoutGrid->insertRow(0);
        layoutGrid->addElement(0, 0, firstTitle);
    }
    firstTitle->setTextColor(plotTextColor());
    firstTitle->setText(title);
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
QCPColorGradient MainWindowPlot::getDefaultGradient()
{
    QCPColorGradient gradient;
    gradient.setColorStopAt(0.0, Qt::blue);
    gradient.setColorStopAt(0.1, QColor(0, 128, 255));
    gradient.setColorStopAt(0.2, QColor(0, 255, 255));
    gradient.setColorStopAt(0.3, QColor(0, 255, 128));
    gradient.setColorStopAt(0.4, Qt::green);
    gradient.setColorStopAt(0.5, QColor(128, 255, 0));
    gradient.setColorStopAt(0.6, Qt::yellow);
    gradient.setColorStopAt(0.7, QColor(255, 165, 0));
    gradient.setColorStopAt(0.8, QColor(255, 100, 0));
    gradient.setColorStopAt(0.9, QColor(255, 50, 0));
    gradient.setColorStopAt(1.0, Qt::red);
    return gradient;
}
/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
QCPColorGradient MainWindowPlot::getStrainGradient()
{
    QCPColorGradient gradient;
    // 应变色标：蓝(负) -> 白(零) -> 红(正)
    // 与用户给出的红白蓝竖向色条一致，瀑布图与色条共享同一渐变。
    gradient.setColorStopAt(0.00, QColor(44, 71, 181));   // deep blue
    gradient.setColorStopAt(0.15, QColor(70, 98, 206));
    gradient.setColorStopAt(0.35, QColor(133, 153, 229));
    gradient.setColorStopAt(0.50, QColor(245, 245, 245)); // near white center
    gradient.setColorStopAt(0.65, QColor(236, 170, 170));
    gradient.setColorStopAt(0.85, QColor(220, 88, 88));
    gradient.setColorStopAt(1.00, QColor(205, 36, 36));   // deep red
    return gradient;
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
QCPColorGradient MainWindowPlot::getIntensityGradient()
{
    QCPColorGradient gradient;
    gradient.setColorStopAt(0.00, QColor(252, 252, 252));
    gradient.setColorStopAt(0.20, QColor(235, 235, 235));
    gradient.setColorStopAt(0.45, QColor(198, 198, 198));
    gradient.setColorStopAt(0.70, QColor(132, 132, 132));
    gradient.setColorStopAt(1.00, QColor(48, 48, 48));
    return gradient;
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::initAudioPlot(QCustomPlot *plot, const QString& title)
{
    if (!plot) return;
    plot->clearGraphs();
    setPlotTitle(plot, title);
    configureAudioWaveformStyle(plot);

    // 坐标轴设置
    plot->xAxis->setLabel("时间(hh:mm:ss)");
    plot->xAxis->setRange(0, 5);
    plot->yAxis->setLabel("相位(rad)");
    plot->yAxis->setRange(-0.1, 0.1);
    plot->axisRect()->setupFullAxesBox();

    // 删除新/老数据图例，只保留单色渲染线
    plot->legend->setVisible(false);

    // 初始化曲线
    plot->addGraph();
    plot->addGraph();
    plot->graph(0)->setVisible(false);
    plot->graph(0)->setSelectable(QCP::stNone);
    plot->graph(0)->setName(QString());
    plot->graph(1)->setSelectable(QCP::stNone);
    plot->graph(1)->setName(QString());
    plot->graph(1)->setPen(audioWaveformPen());
    plot->graph(1)->setLineStyle(QCPGraph::lsLine);
    plot->graph(1)->setScatterStyle(QCPScatterStyle::ssNone);
    plot->graph(1)->setAdaptiveSampling(false);
    plot->graph(1)->setAntialiased(false);

    // 时间轴
    QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
    timeTicker->setTimeFormat("%h:%m:%s");
    plot->xAxis->setTicker(timeTicker);

    // 同步轴范围
    connect(plot->xAxis, SIGNAL(rangeChanged(QCPRange)), plot->xAxis2, SLOT(setRange(QCPRange)));
    connect(plot->yAxis, SIGNAL(rangeChanged(QCPRange)), plot->yAxis2, SLOT(setRange(QCPRange)));

    // 交互设置：禁用曲线选择（避免点击后变色），保留拖拽/缩放。
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    configureAxisZoomDrag(plot);
    plot->setNoAntialiasingOnDrag(true);
    plot->setPlottingHints(QCP::phFastPolylines | QCP::phCacheLabels);
    plot->setAntialiasedElements(QCP::aeNone);
    plot->setNotAntialiasedElements(QCP::aeAll);

   // qDebug() << "方差用时";
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::initRMSWaterPlot(QCustomPlot *plot, int realRows, int extractCount, int frequency)
{
    if (!plot) return;
    MainWindowUtils::setPlotBackground(plot);
    clearPlotLayoutDecorations(plot);
    plot->clearPlottables();


    // 3. 轴矩形基础设置
    QCPAxisRect *axisRect = plot->axisRect();
    if (axisRect) {
        axisRect->setupFullAxesBox();
        axisRect->axis(QCPAxis::atBottom)->setRange(0, 1);
        axisRect->axis(QCPAxis::atLeft)->setRange(0, 1);
    }

    // ========== 基础结构初始化（仅首次创建） ==========
    setPlotTitle(plot, "振动定位瀑布图(RMS)");

    // 坐标轴
    plot->xAxis->setLabel("位置(m)");
    plot->yAxis->setLabel("时间(S)");
    int maxX = MainWindowUtils::calculateMaxX(realRows, extractCount, frequency);

    plot->xAxis->setRange(0, maxX);
    plot->yAxis->setRange(0, 100);

    // 颜色映射（核心：创建一次，后续仅改样式）
    QCPColorMap *rmsColorMap = new QCPColorMap(plot->xAxis, plot->yAxis);
    rmsColorMap->data()->setSize(realRows, 100);
    rmsColorMap->data()->setRange(QCPRange(0, maxX), QCPRange(0, 100));

    // 色条（创建一次，后续仅改样式）
    QCPColorScale *colorScale = new QCPColorScale(plot);
    colorScale->setType(QCPAxis::atBottom);
    colorScale->setBarWidth(10);
    colorScale->setRangeDrag(true);
    colorScale->setRangeZoom(true);
    colorScale->axis()->setBasePen(QPen(plotAxisColor()));
    colorScale->axis()->setTickPen(QPen(plotAxisColor()));
    colorScale->axis()->setSubTickPen(QPen(plotAxisColor()));
    colorScale->axis()->setTickLabelColor(plotTextColor());
    colorScale->axis()->setLabelColor(plotTextColor());
    colorScale->axis()->setLabel("RMS值");

    plot->plotLayout()->insertRow(2);
    plot->plotLayout()->addElement(2, 0, colorScale);

    rmsColorMap->setColorScale(colorScale);
    rmsColorMap->setGradient(getDefaultGradient()); // RMS默认渐变

    // 边距对齐（仅创建一次）
    QCPMarginGroup *marginGroup = new QCPMarginGroup(plot);
    plot->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
    colorScale->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    rmsColorMap->rescaleDataRange(true);
    // RMS 色条初始范围固定为 0~6
    rmsColorMap->setDataRange(QCPRange(0.0, 6.0));
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    configureAxisZoomDrag(plot);

    plot->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::initRMSLinePlot(QCustomPlot *plot,
                                     int realRows,
                                     int extractCount,
                                     int frequency,
                                     bool strainMode,
                                     bool channelXAxisMode)
{
    if (!plot) return;
    MainWindowUtils::setPlotBackground(plot);
    clearPlotLayoutDecorations(plot);
    plot->clearPlottables();

    const double maxX = channelXAxisMode
                            ? static_cast<double>(qMax(1, realRows))
                            : static_cast<double>(MainWindowUtils::calculateMaxX(realRows, extractCount, frequency));

    setPlotTitle(plot, strainMode ? QStringLiteral("应变定位曲线") : QStringLiteral("振动定位曲线(RMS)"));
    plot->legend->setVisible(false);
    plot->xAxis->setLabel(channelXAxisMode ? QStringLiteral("通道") : QStringLiteral("位置(m)"));
    plot->yAxis->setLabel(strainMode ? QStringLiteral("应变(με)") : QStringLiteral("RMS值"));
    plot->xAxis->setRange(0, qMax(1.0, maxX));
    plot->yAxis->setRange(strainMode ? QCPRange(-50, 50) : QCPRange(0, 6));
    plot->axisRect()->setupFullAxesBox();

    plot->addGraph();
    plot->graph(0)->setSelectable(QCP::stNone);
    plot->graph(0)->setName(QString());
    plot->graph(0)->setPen(QPen(strainMode ? QColor(255, 124, 124) : QColor(80, 178, 255), 1.6));
    plot->graph(0)->setAdaptiveSampling(true);
    plot->graph(0)->setAntialiased(false);

    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    configureAxisZoomDrag(plot);
    plot->setNoAntialiasingOnDrag(true);
    plot->setPlottingHints(QCP::phFastPolylines | QCP::phCacheLabels);
    plot->setAntialiasedElements(QCP::aeNone);

    plot->replot(QCustomPlot::rpQueuedReplot);
}
/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::RMStoStrainWaterPlot(QCustomPlot *plot, int realRows, int extractCount, int frequency)
{
    Q_UNUSED(realRows);
    Q_UNUSED(extractCount);
    Q_UNUSED(frequency);
    if (!plot) return;

    setPlotTitle(plot, "应变瀑布图");

    if (plot->plottableCount() <= 0) return;
    QCPColorMap *map = qobject_cast<QCPColorMap*>(plot->plottable(0));
    if (!map) return;

    QCPLayoutGrid *layoutGrid = qobject_cast<QCPLayoutGrid*>(plot->plotLayout());
    QCPColorScale *firstScale = nullptr;
    if (layoutGrid) {
        for (int i = layoutGrid->elementCount() - 1; i >= 0; --i) {
            QCPLayoutElement *elem = layoutGrid->elementAt(i);
            QCPColorScale *scale = qobject_cast<QCPColorScale*>(elem);
            if (!scale) continue;
            if (!firstScale) firstScale = scale;
            else {
                layoutGrid->takeAt(i);
                delete scale;
            }
        }
    }
    if (!firstScale) {
        firstScale = new QCPColorScale(plot);
        firstScale->setType(QCPAxis::atBottom);
        firstScale->setBarWidth(10);
        firstScale->setRangeDrag(true);
        plot->plotLayout()->insertRow(2);
        plot->plotLayout()->addElement(2, 0, firstScale);
    }

    map->setColorScale(firstScale);
    firstScale->setRangeDrag(true);
    firstScale->setRangeZoom(true);
    firstScale->axis()->setLabel("应变(με)");
    map->setDataRange(QCPRange(-0.5, 0.5));
    map->setGradient(getStrainGradient());
    clearColorMapCells(map);
    plot->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::StrainToRMSWaterPlot(QCustomPlot *plot, int realRows, int extractCount, int frequency)
{
    Q_UNUSED(realRows);
    Q_UNUSED(extractCount);
    Q_UNUSED(frequency);
    if (!plot) return;

    setPlotTitle(plot, "振动定位瀑布图(RMS)");

    if (plot->plottableCount() <= 0) return;
    QCPColorMap *map = qobject_cast<QCPColorMap*>(plot->plottable(0));
    if (!map) return;

    QCPLayoutGrid *layoutGrid = qobject_cast<QCPLayoutGrid*>(plot->plotLayout());
    QCPColorScale *firstScale = nullptr;
    if (layoutGrid) {
        for (int i = layoutGrid->elementCount() - 1; i >= 0; --i) {
            QCPLayoutElement *elem = layoutGrid->elementAt(i);
            QCPColorScale *scale = qobject_cast<QCPColorScale*>(elem);
            if (!scale) continue;
            if (!firstScale) firstScale = scale;
            else {
                layoutGrid->takeAt(i);
                delete scale;
            }
        }
    }
    if (!firstScale) {
        firstScale = new QCPColorScale(plot);
        firstScale->setType(QCPAxis::atBottom);
        firstScale->setBarWidth(10);
        firstScale->setRangeDrag(true);
        plot->plotLayout()->insertRow(2);
        plot->plotLayout()->addElement(2, 0, firstScale);
    }

    map->setColorScale(firstScale);
    firstScale->setRangeDrag(true);
    firstScale->setRangeZoom(true);
    firstScale->axis()->setLabel("RMS值");
    map->setDataRange(QCPRange(0.0, 6.0));
    map->setGradient(getDefaultGradient());
    clearColorMapCells(map);
    plot->replot(QCustomPlot::rpQueuedReplot);
}


/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::initFFTWaterPlot(QCustomPlot *plot, int frequency)
{
    if (!plot) return;
    if (frequency <= 0) frequency = 10000;
    MainWindowUtils::setPlotBackground(plot);

    // Re-init safely: remove existing title/color-scale elements first.
    QCPLayoutGrid *layoutGrid = qobject_cast<QCPLayoutGrid*>(plot->plotLayout());
    if (layoutGrid) {
        for (int i = layoutGrid->elementCount() - 1; i >= 0; --i) {
            QCPLayoutElement *elem = layoutGrid->elementAt(i);
            if (qobject_cast<QCPTextElement*>(elem) || qobject_cast<QCPColorScale*>(elem)) {
                layoutGrid->takeAt(i);
                delete elem;
            }
        }
        layoutGrid->simplify();
    }

    plot->clearPlottables();
    setPlotTitle(plot, "频谱瀑布图");

    // 坐标轴
    plot->xAxis->setLabel("频率(Hz)");
    plot->yAxis->setLabel("时间(S)");
    plot->xAxis->setRange(0, frequency / 2);
    plot->yAxis->setRange(0, 100);
    plot->axisRect()->setupFullAxesBox();

    // 颜色映射
    QCPColorMap *colorMap = new QCPColorMap(plot->xAxis, plot->yAxis);
    colorMap->data()->setSize(2501, 100);
    colorMap->data()->setRange(QCPRange(0, frequency / 2 + 2), QCPRange(0, 100));
        //qDebug() << "frequency"<<frequency;
    // 色条
    QCPColorScale *colorScale = new QCPColorScale(plot);
    colorScale->setType(QCPAxis::atBottom);
    colorScale->setBarWidth(10);
    colorScale->setRangeDrag(true);
    colorScale->setRangeZoom(true);
    colorScale->axis()->setBasePen(QPen(plotAxisColor()));
    colorScale->axis()->setTickPen(QPen(plotAxisColor()));
    colorScale->axis()->setSubTickPen(QPen(plotAxisColor()));
    colorScale->axis()->setTickLabelColor(plotTextColor());
    colorScale->axis()->setLabelColor(plotTextColor());
    plot->plotLayout()->insertRow(2);
    plot->plotLayout()->addElement(2, 0, colorScale);
    colorMap->setColorScale(colorScale);
    colorMap->setGradient(getDefaultGradient());

    // 边距对齐
    QCPMarginGroup *marginGroup = new QCPMarginGroup(plot);
    plot->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
    colorScale->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    colorMap->rescaleDataRange(true);
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    configureAxisZoomDrag(plot);
    plot->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::initFFTSpectrumPlot(QCustomPlot *plot, int frequency)
{
    if (!plot) return;
    if (frequency <= 0) frequency = 10000;
    MainWindowUtils::setPlotBackground(plot);

    QCPLayoutGrid *layoutGrid = qobject_cast<QCPLayoutGrid*>(plot->plotLayout());
    if (layoutGrid) {
        for (int i = layoutGrid->elementCount() - 1; i >= 0; --i) {
            QCPLayoutElement *elem = layoutGrid->elementAt(i);
            if (qobject_cast<QCPTextElement*>(elem) || qobject_cast<QCPColorScale*>(elem)) {
                layoutGrid->takeAt(i);
                delete elem;
            }
        }
        layoutGrid->simplify();
    }

    plot->clearPlottables();
    setPlotTitle(plot, "频谱图");
    plot->legend->setVisible(false);

    plot->xAxis->setLabel("频率(Hz)");
    plot->yAxis->setLabel("幅值");
    plot->xAxis->setRange(0, frequency / 2.0);
    plot->yAxis->setRange(0, 1);
    plot->axisRect()->setupFullAxesBox();

    plot->addGraph();
    plot->graph(0)->setPen(QPen(QColor(255, 165, 55), 1.3));
    plot->graph(0)->setLineStyle(QCPGraph::lsLine);
    plot->graph(0)->setScatterStyle(QCPScatterStyle::ssNone);
    plot->graph(0)->setAdaptiveSampling(true);
    plot->graph(0)->setAntialiased(false);

    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    configureAxisZoomDrag(plot);
    plot->setNoAntialiasingOnDrag(true);
    plot->setPlottingHints(QCP::phFastPolylines | QCP::phCacheLabels);
    plot->setAntialiasedElements(QCP::aeNone);
    plot->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口图表初始化层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindowPlot::initIntensityPlot(QCustomPlot *plot)
{
    if (!plot) return;
    MainWindowUtils::setPlotBackground(plot);
    clearPlotLayoutDecorations(plot);
    plot->clearPlottables();
    setPlotTitle(plot, "光强图");

    // 坐标轴
    plot->xAxis->setLabel("位置(m)");
    plot->xAxis->setRange(0,1000);
    plot->yAxis->setLabel("光强/db");
    plot->yAxis->setRange(0,100);
    plot->axisRect()->setupFullAxesBox();

    // 颜色映射
    QCPColorMap *colorMap = new QCPColorMap(plot->yAxis, plot->xAxis);
    colorMap->data()->setSize(101,1001);
    colorMap->data()->setRange(QCPRange(0,100),QCPRange(0,1001));

    // 色条
    QCPColorScale *colorScale = new QCPColorScale(plot);
    colorScale->setType(QCPAxis::atBottom);
    colorScale->setRangeDrag(true);
    colorScale->axis()->setBasePen(QPen(plotAxisColor()));
    colorScale->axis()->setTickPen(QPen(plotAxisColor()));
    colorScale->axis()->setSubTickPen(QPen(plotAxisColor()));
    colorScale->axis()->setTickLabelColor(plotTextColor());
    colorScale->axis()->setLabelColor(plotTextColor());
    plot->plotLayout()->addElement(1,0,colorScale);
    colorMap->setColorScale(colorScale);
    colorMap->setGradient(getIntensityGradient());

    // 边距对齐
    QCPMarginGroup *marginGroup = new QCPMarginGroup(plot);
    plot->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
    colorScale->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    colorMap->setDataRange(QCPRange(0.0, 100.0));
    clearColorMapCells(colorMap);
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    configureAxisZoomDrag(plot);
    plot->replot(QCustomPlot::rpQueuedReplot);
}


