/**
 * @file mainwindow.cpp
 * @brief 主窗口的 UI 装配、参数校验、保存操作、离线查看和显示模式控制。
 *
 * 该实现将 .ui 中的控件转化为 ParameterManager 的运行参数，协调 MainWindowPlot（图结构）和
 * MainWindowData（实时渲染），并通过信号与后台处理线程交互。所有来自工作线程的消息都在此回到 GUI 线程处理。
 */
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDebug>
#include <QIcon>
#include <QGraphicsDropShadowEffect>
#include <QFont>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QIntValidator>
#include <QStyle>
#include <QStorageInfo>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringConverter>
#include <QSignalBlocker>
#include <QDoubleValidator>
#include <QGuiApplication>
#include <QScreen>
#include <QtMath>
#include <cmath>
#include "ParameterManager.h"
#include "saveddataviewer.h"

namespace {
constexpr double kAudioHistorySeconds = 3.0 * 60.0 * 60.0;
const QStringList kConfigKeys = {
    QStringLiteral("pulse_frequency"),
    QStringLiteral("extract_count"),
    QStringLiteral("start_save"),
    QStringLiteral("end_save"),
    QStringLiteral("monitorPosition"),
    QStringLiteral("monitorPosition1"),
    QStringLiteral("monitorPosition2"),
    QStringLiteral("singleSaveRow"),
    QStringLiteral("differential_distance"),
    QStringLiteral("enable_diff_phase"),
    QStringLiteral("enable_filter"),
    QStringLiteral("high_pass_cutoff_hz"),
    QStringLiteral("numThreads")
};

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
bool parseBoolText(const QString &text, bool *ok)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("1") ||
        normalized == QStringLiteral("true") ||
        normalized == QStringLiteral("yes") ||
        normalized == QStringLiteral("on")) {
        if (ok) *ok = true;
        return true;
    }
    if (normalized == QStringLiteral("0") ||
        normalized == QStringLiteral("false") ||
        normalized == QStringLiteral("no") ||
        normalized == QStringLiteral("off")) {
        if (ok) *ok = true;
        return false;
    }
    if (ok) *ok = false;
    return false;
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
bool isSupportedPulseFrequency(int frequency)
{
    return frequency == 20000 ||
           frequency == 10000 ||
           frequency == 3333 ||
           frequency == 2000;
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
bool isSupportedExtractCount(int extractCount)
{
    return extractCount == 1 ||
           extractCount == 2 ||
           extractCount == 4 ||
           extractCount == 8 ||
           extractCount == 16;
}
}

// ========== MainWindow构造/析构 ==========
/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{



    // 先确保UI加载（这一步是UI显示的基础）
    if (!ui) {
        qCritical() << "[MainWindow构造] UI指针为空，无法加载界面！";
        return; // 直接返回，避免后续崩溃
    }
    ui->setupUi(this);


    setWindowTitle(QStringLiteral("啁啾扫频型分布式光纤测井设备"));
    setWindowIcon(QIcon(":/app_icon.ico"));
    applyUiTheme();
    setupAudioPanels();
    setupPlotToolButtons();
    setWindowState(windowState() | Qt::WindowMaximized);


    // 初始化参数管理器（核心配置）
    ParameterManager::instance().setParameter("start_save", 0);
    ParameterManager::instance().setParameter("end_save", 0);
    ParameterManager::instance().setParameter("monitorPosition", 200);
    ParameterManager::instance().setParameter("monitorPosition1", 200);
    ParameterManager::instance().setParameter("monitorPosition2", 300);
    ParameterManager::instance().setParameter("singleSaveRow", 200);
    ParameterManager::instance().setParameter("pulse_frequency", 10000);
    ParameterManager::instance().setParameter("extract_count", 8);
    ParameterManager::instance().setParameter("differential_distance", 8);
    ParameterManager::instance().setParameter("enable_diff_phase", false);
    ParameterManager::instance().setParameter("enable_filter", true);
    ParameterManager::instance().setParameter("high_pass_cutoff_hz", 1.0);
    ParameterManager::instance().setParameter("numThreads", 6);
    ParameterManager::instance().setParameter("save_all_active", false);
    ParameterManager::instance().setParameter("save_single_active", false);
    ParameterManager::instance().setParameter("active_single_save_row", -1);
    ParameterManager::instance().setParameter("saved_data_viewer_busy", false);
    if (m_diffPhaseCheckBox) {
        m_diffPhaseCheckBox->setChecked(false);
    }


    // 初始化所有模块（删除m_interactModule）
    initModules();


    // 初始化UI控件指针（替代原setUIWidgets）
    initUIWidgetPointers();


    // 连接所有信号槽（直接连接到MainWindow自身）
    connectSignalsSlots();
    syncMonitorChannelRange(true);


    // ========== 第二步：关键空指针校验（模块初始化失败会导致后续崩溃） ==========
    if (!m_plotModule) {
        qCritical() << "[MainWindow构造] m_plotModule为空，跳过图表初始化！";
    } else {
        // 初始化图表（首次）
        for (int i = 0; i < m_audioPlots.size(); ++i) {
            m_plotModule->initAudioPlot(m_audioPlots[i], QString());
        }
        for (int i = 0; i < m_audioPlots.size(); ++i) {
            if (!m_audioPlots[i]) continue;
            m_audioPlots[i]->xAxis->setVisible(true);
            m_audioPlots[i]->xAxis->setTickLabels(true);
            m_audioPlots[i]->xAxis->setLabel(QStringLiteral("时间(hh:mm:ss)"));
            m_audioPlots[i]->axisRect()->setAutoMargins(QCP::msAll);
        }

        m_plotModule->initFFTWaterPlot(ui->widget_3, 10000);

        m_plotModule->initIntensityPlot(ui->widget_4);


        // 给数据渲染器绑定图表
        if (!m_dataModule) {
            qCritical() << "[MainWindow构造] m_dataModule为空，跳过图表绑定！";
        } else {
            m_dataModule->setAudioPlots(m_audioPlots);
            m_dataModule->setAudioYAxisAutoScale(0, false);
            m_dataModule->setFFTWaterPlot(ui->widget_3);
            m_dataModule->setFftLineMode(m_fftLineMode);
        }
        syncRmsPlotMode();
    }

    if (ui && ui->filter) {
        onFilterStateChanged(ui->filter->checkState());
        on_filter_stateChanged(ui->filter->checkState());
    }
    syncConfigUiFromParameters();

    updateOverlayButtonsGeometry();
    updateSingleSaveButtons();
    updateSaveAllUiState();
    updateMonitorScaleLabels();
    updateRmsXAxisModeButtonText();
    updateRmsModeButtonText();
    updateRmsPauseButtonText();
    if (ui && ui->label_14) {
        ui->label_14->setText("-- GB");
    }
    QStorageInfo currentStorage(QDir::currentPath());
    currentStorage.refresh();
    if (currentStorage.isValid() && currentStorage.isReady()) {
        onDiskSpaceUpdated(currentStorage.bytesAvailable() / (1024.0 * 1024.0 * 1024.0));
    }

}

/**
 * @brief 完成窗口或管理对象析构时的资源断开与安全收尾。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
MainWindow::~MainWindow()
{
    beginShutdown();
    delete ui;
    ui = nullptr;
    // 模块由Qt父子机制自动销毁（parent=this）
}

/**
 * @brief 清空或复位当前功能相关的状态，确保后续流程从一致状态继续。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::beginShutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;

    QObject::disconnect(nullptr, nullptr, this, nullptr);
    QObject::disconnect(this, nullptr, nullptr, nullptr);

    if (m_dataModule) {
        m_dataModule->shutdown();
    }
}

// ========== 核心初始化函数 ==========
/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::initModules()
{
    // 初始化模块（删除m_interactModule的创建）
    m_plotModule = new MainWindowPlot(this);
    m_dataModule = new MainWindowData(this);
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::initUIWidgetPointers()
{
    // 替代原MainWindowInteraction的setUIWidgets方法，直接绑定UI控件
    m_lengthCombo = ui->length_selection_comboBox;
    m_channelEdit = ui->channel_label_edit;
    m_extractCombo = ui->extract_edit;
    m_diffDistEdit = ui->lineEdit_3;
    m_startSaveEdit = ui->lineEdit;
    m_endSaveEdit = ui->lineEdit_2;
    m_filterBox = ui->filter;

    if (m_startSaveEdit) {
        m_startSaveEdit->setValidator(new QIntValidator(0, 999999, m_startSaveEdit));
    }
    if (m_endSaveEdit) {
        m_endSaveEdit->setValidator(new QIntValidator(0, 999999, m_endSaveEdit));
    }
}

void MainWindow::connectSignalsSlots()
{
    // 1. 按钮控制（开始/停止）
    connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    connect(ui->pushButton_2, &QPushButton::clicked, this, &MainWindow::onStopButtonClicked);

    // 2. 原MainWindowInteraction的UI按钮直接连接到MainWindow自身的槽函数
    connect(ui->extract_button, &QPushButton::clicked, this, &MainWindow::onExtractButtonClicked);
    connect(ui->pulse_frequency_button, &QPushButton::clicked, this, &MainWindow::onPulseFrequencyButtonClicked);
    connect(ui->differential_distance, &QPushButton::clicked, this, &MainWindow::onDiffDistanceButtonClicked);
    connect(ui->begin_button, &QPushButton::clicked, this, &MainWindow::onStartSaveButtonClicked);
    connect(ui->end_button, &QPushButton::clicked, this, &MainWindow::onEndSaveButtonClicked);
    if (m_startSaveEdit) {
        connect(m_startSaveEdit, &QLineEdit::returnPressed, this, &MainWindow::onStartSaveButtonClicked);
    }
    if (m_endSaveEdit) {
        connect(m_endSaveEdit, &QLineEdit::returnPressed, this, &MainWindow::onEndSaveButtonClicked);
    }
    connect(ui->save_single_button, &QPushButton::clicked, this, &MainWindow::onSaveOnlyButtonClicked);
    connect(ui->save_all_button, &QPushButton::clicked, this, &MainWindow::onSaveAllButtonClicked);
    connect(ui->filter, &QCheckBox::stateChanged, this, &MainWindow::onFilterStateChanged);
    connect(ui->length_selection_comboBox, &QComboBox::currentIndexChanged, this, &MainWindow::onLengthComboIndexChanged);
    if (m_diffPhaseCheckBox) {
        connect(m_diffPhaseCheckBox, &QCheckBox::stateChanged, this, &MainWindow::onDiffPhaseStateChanged);
    }
    if (m_highPassConfirmButton) {
        connect(m_highPassConfirmButton, &QPushButton::clicked, this, &MainWindow::onHighPassConfirmButtonClicked);
    }
    if (m_highPassCutoffEdit) {
        connect(m_highPassCutoffEdit, &QLineEdit::returnPressed, this, &MainWindow::onHighPassConfirmButtonClicked);
    }
    if (m_saveConfigButton) {
        connect(m_saveConfigButton, &QPushButton::clicked, this, &MainWindow::onSaveConfigClicked);
    }
    if (m_loadConfigButton) {
        connect(m_loadConfigButton, &QPushButton::clicked, this, &MainWindow::onLoadConfigClicked);
    }

    // 3. 结束保存按钮
    connect(ui->end_save_button, &QPushButton::clicked, this, &MainWindow::end_save);
    connect(ui->end_save_all_button, &QPushButton::clicked, this, &MainWindow::end_save_all);
}

// ========== 原MainWindow的核心槽函数 ==========
/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onStartButtonClicked()
{
    // m_dataModule->startRenderTimers();
    ui->statusBar->showMessage("######开始######");
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onStopButtonClicked()
{
    //  m_dataModule->stopRenderTimers();
    ui->statusBar->showMessage("######停止######");
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onReceivePcieTime1(int mode, qint64 time, int freq)
{
    if (m_shuttingDown || !ui || !ui->timelabel1) return;
    Q_UNUSED(mode);
    MainWindowUtils::updateTimeLabel(ui->timelabel1, time, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onReceiveProcessingTime(qint64 time, int freq)
{
    if (m_shuttingDown || !ui || !ui->timelabel2) return;
    MainWindowUtils::updateTimeLabel(ui->timelabel2, time, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onPreserveProcessingTime(qint64 time, int freq)
{
    if (m_shuttingDown || !ui || !ui->timelabel3) return;
    MainWindowUtils::updateTimeLabel(ui->timelabel3, time, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onFilterProcessingTime(qint64 time, int freq)
{
    if (m_shuttingDown || !ui || !ui->timelabel4) return;
    MainWindowUtils::updateTimeLabel(ui->timelabel4, time, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onRmsProcessingTime(qint64 time, int freq)
{
    if (m_shuttingDown || !ui || !ui->timelabel5) return;
    MainWindowUtils::updateTimeLabel(ui->timelabel5, time, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onFftProcessingTime(qint64 time, int freq)
{
    if (m_shuttingDown || !ui || !ui->timelabel6) return;
    MainWindowUtils::updateTimeLabel(ui->timelabel6, time, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onSaveProcessingTime(qint64 time, int freq)
{
    if (m_shuttingDown || !ui || !ui->label_13) return;
    MainWindowUtils::updateTimeLabel(ui->label_13, time, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onReceiveMultiAudioData(const QList<QVector<float>>& rowsData,
                                         const QVector<int>& channelRows,
                                         int len,
                                         int freq,
                                         int extractCount)
{
    if (m_shuttingDown || !m_dataModule) return;
    if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) return;
    Q_UNUSED(channelRows);
    m_frequency = freq;
    m_extractCount = extractCount;
    m_dataModule->onReceiveMultiAudioData(rowsData, len, freq);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onRequestSelectFolder(const QString& saveType)
{
    if (m_shuttingDown) return;
    QString dir = QFileDialog::getExistingDirectory(this,
                                                    saveType == "saveAll" ? "选择保存全部数据的文件夹" : "选择保存单点数据的文件夹",
                                                    QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    emit folderSelectedToThread(saveType, dir);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onDiskSpaceUpdated(double freeGb)
{
    if (!ui || !ui->label_14) return;
    if (!std::isfinite(freeGb) || freeGb < 0.0) {
        ui->label_14->setText("-- GB");
        return;
    }

    ui->label_14->setText(QString("%1 GB").arg(freeGb, 0, 'f', 2));
    if (freeGb < 1.0) {
        ui->label_14->setStyleSheet("color:#d9534f; font-weight:700;");
    } else if (freeGb < 5.0) {
        ui->label_14->setStyleSheet("color:#e0921b; font-weight:700;");
    } else {
        ui->label_14->setStyleSheet("color:#1f6fd6; font-weight:600;");
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onSingleSaveStateChanged(bool active)
{
    if (m_shuttingDown) return;
    m_singleSaveActive = active;
    if (active) {
        m_activeSavePanel = (m_pendingSavePanel >= 0) ? m_pendingSavePanel : m_activeSavePanel;
        if (m_activeSavePanel < 0 && !m_audioSaveButtons.isEmpty()) m_activeSavePanel = 0;
    } else {
        m_activeSavePanel = -1;
    }
    m_pendingSavePanel = -1;
    updateSingleSaveButtons();
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onSaveAllStateChanged(bool active)
{
    if (m_shuttingDown) return;
    m_saveAllActive = active;
    m_saveAllPending = false;
    updateSaveAllUiState();
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::on_filter_stateChanged(int state)
{
    if (!m_plotModule || !ui || !ui->widget_2) return;

    const bool strainMode = (state != Qt::Checked);
    const int freq = ParameterManager::instance().getParameter("pulse_frequency").toInt();
    const int extract = ParameterManager::instance().getParameter("extract_count").toInt();
    m_frequency = freq;
    m_extractCount = extract;
    syncRmsPlotMode();

    if (m_dataModule) {
        m_dataModule->setStrainMode(strainMode);
        m_dataModule->resetAudioTimeline();
    }

    for (int i = 0; i < m_audioPlots.size(); ++i) {
        QCustomPlot *plot = m_audioPlots[i];
        if (!plot) continue;
        plot->yAxis->setLabel(strainMode ? QStringLiteral("应变(με)") : QStringLiteral("相位(rad)"));
        if (plot->graphCount() > 1 && plot->graph(1)) {
            plot->graph(1)->data()->clear();
        }
        plot->xAxis->setRange(0, 5);
        plot->yAxis->setRange(strainMode ? QCPRange(-50, 50) : QCPRange(-0.1, 0.1));
        plot->replot(QCustomPlot::rpQueuedReplot);
    }

    if (ui->statusBar) {
        ui->statusBar->showMessage(strainMode ? "已切换到应变模式(με)" : "已切换到相位/RMS模式", 2000);
    }
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::showMessage(const QString& title, const QString& text, QMessageBox::StandardButtons btns)
{
    if (m_shuttingDown) return;
    QMessageBox* msg = new QMessageBox(this);
    msg->setWindowTitle(title);
    msg->setText(text);
    msg->setStandardButtons(btns);
    msg->setWindowModality(Qt::NonModal);
    connect(msg, &QMessageBox::finished, msg, &QMessageBox::deleteLater);
    msg->show();
}

// ========== 原MainWindowInteraction的所有方法（整合到MainWindow） ==========
/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindow::getPulseFreqByLength(const QString& lengthText)
{
    const QString normalized = lengthText.trimmed().toLower().remove(' ');

    // New UI options: 5km(20Khz), 10km(10Khz), 30km(3.333Khz), 50km(2Khz)
    if (normalized.contains("5km") && normalized.contains("20khz")) return 20000;
    if (normalized.contains("10km") && normalized.contains("10khz")) return 10000;
    if (normalized.contains("30km") &&
        (normalized.contains("3.333khz") || normalized.contains("3.33khz") || normalized.contains("3.33k5"))) {
        return 3333;
    }
    if (normalized.contains("50km") && normalized.contains("2khz")) return 2000;

    // Backward compatibility for legacy texts.
    if (normalized == "50km" || normalized == "2khz") return 2000;
    if (normalized == "30km" || normalized == "3.333khz" || normalized == "3.33khz") return 3333;
    if (normalized == "10km" || normalized == "10khz") return 10000;
    if (normalized == "5km" || normalized == "20khz" || normalized == "5khz") return 20000;
    if (normalized == "1km" || normalized == "100khz") return 100000;
    return ParameterManager::instance().getParameter("pulse_frequency").toInt();
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindow::getRowsByFrequency(int frequency) const
{
    if (frequency == 20000) return 1024 * 11;
    if (frequency == 10000 || frequency == 100000) return 1024 * 23;
    if (frequency == 3333 || frequency == 2000) return 1024 * 24;

    const int safeExtract = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    return qMax(1, m_realRows * safeExtract);
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindow::getOutputRowCount(int frequency,
                                  int extractCount,
                                  int startChannel,
                                  int endChannel,
                                  int differentialDistance) const
{
    const int safeExtract = qMax(1, extractCount);
    const int safeDiff = qMax(1, differentialDistance);
    const int rawRows = getRowsByFrequency(frequency);
    const qint64 startRaw64 = static_cast<qint64>(qMax(0, startChannel)) * safeExtract;
    const qint64 endRaw64 = static_cast<qint64>(qMax(startChannel, endChannel)) * safeExtract;
    const int startRaw = static_cast<int>(
        qBound<qint64>(0LL, startRaw64, static_cast<qint64>(rawRows - 1)));
    const int endRaw = static_cast<int>(
        qBound<qint64>(static_cast<qint64>(startRaw),
                       endRaw64,
                       static_cast<qint64>(rawRows)));
    const int lastBaseExclusive = qMin(endRaw, rawRows - safeDiff);
    if (startRaw >= lastBaseExclusive) return 0;
    return 1 + (lastBaseExclusive - 1 - startRaw) / safeExtract;
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindow::getMaxMonitorChannel(int frequency, int extractCount) const
{
    const int startChannel = ParameterManager::instance().getParameter("start_save").toInt();
    const int endChannel = ParameterManager::instance().getParameter("end_save").toInt();
    const int differentialDistance =
        ParameterManager::instance().getParameter("differential_distance").toInt();
    return qMax(0,
                getOutputRowCount(frequency,
                                  extractCount,
                                  startChannel,
                                  endChannel,
                                  differentialDistance) - 1);
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindow::getMaxSaveStartChannel(int frequency, int extractCount) const
{
    const int safeExtract = qMax(1, extractCount);
    const int rawRows = getRowsByFrequency(frequency);
    const int differentialDistance = qMax(
        1,
        ParameterManager::instance().getParameter("differential_distance").toInt());
    return qMax(0, (rawRows - differentialDistance - 1) / safeExtract);
}

/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindow::getMaxSaveEndChannel(int frequency, int extractCount) const
{
    const int safeExtract = qMax(1, extractCount);
    const int rows = getRowsByFrequency(frequency);
    return qMax(1, rows / safeExtract);
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::syncMonitorChannelRange(bool resetStart)
{
    const int freq = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int extract = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    const int maxStartChannel = getMaxSaveStartChannel(freq, extract);
    const int maxEndChannel = getMaxSaveEndChannel(freq, extract);

    int startChannel = ParameterManager::instance().getParameter("start_save").toInt();
    if (resetStart) {
        startChannel = 0;
    }
    startChannel = qBound(0, startChannel, maxStartChannel);

    int endCandidate = ParameterManager::instance().getParameter("end_save").toInt();
    if (m_endSaveEdit) {
        bool uiOk = false;
        const double uiEnd = m_endSaveEdit->text().toDouble(&uiOk);
        if (uiOk) {
            endCandidate = qCeil(uiEnd);
        }
    }

    const int minEndChannel = qMin(maxEndChannel, startChannel + 1);
    int endChannel = maxEndChannel;
    if (m_endChannelUserEdited) {
        endChannel = qBound(minEndChannel, endCandidate, maxEndChannel);
    }

    ParameterManager::instance().setParameter("start_save", startChannel);
    ParameterManager::instance().setParameter("end_save", endChannel);

    if (m_startSaveEdit) m_startSaveEdit->setText(QString::number(startChannel));
    if (m_endSaveEdit) m_endSaveEdit->setText(QString::number(endChannel));

    const int maxMonitorChannel = getMaxMonitorChannel(freq, extract);
    for (int i = 0; i < m_audioSelectedChannels.size(); ++i) {
        const int channel = qBound(0, m_audioSelectedChannels.value(i, 0), maxMonitorChannel);
        if (i < m_audioChannelEdits.size() && m_audioChannelEdits[i]) {
            m_audioChannelEdits[i]->setText(QString::number(channel));
        }
        applyAudioChannelSelection(i);
    }

    updateMonitorScaleLabels();
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onChannelButtonClicked()
{
    if (!m_channelEdit) return;
    bool ok;
    float input = m_channelEdit->text().toFloat(&ok);
    if (!ok)
    {
        QMessageBox::warning(nullptr, "输入无效", "请输入有效的数字！");
        return;
    }

    // 输入按米理解，折算为当前抽取后通道号
    int freq = ParameterManager::instance().getParameter("pulse_frequency").toInt();
    int extract = ParameterManager::instance().getParameter("extract_count").toInt();
    const double meterPerChannel = MainWindowUtils::meterPerExtractedChannel(freq, extract);
    const int adjusted = qBound(0,
                                static_cast<int>(qRound(input / qMax(1e-9, meterPerChannel))),
                                getMaxMonitorChannel(freq, extract));
    ParameterManager::instance().setParameter("monitorPosition", adjusted);
    ParameterManager::instance().setParameter("monitorPosition1", adjusted);
    if (!m_audioChannelEdits.isEmpty()) {
        m_audioChannelEdits[0]->setText(QString::number(adjusted));
        applyAudioChannelSelection(0);
    }

    // 切换音频图颜色（简化，实际可通过信号传递给MainWindowData）
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onExtractButtonClicked()
{
    if (!m_extractCombo) return;
    int extract = m_extractCombo->currentData().toInt();
    if (extract <= 0) {
        QMessageBox::warning(nullptr, "输入无效", "抽取系数配置无效，请重新选择！");
        return;
    }

    ParameterManager::instance().setParameter("extract_count", extract);
    const int freq = ParameterManager::instance().getParameter("pulse_frequency").toInt();
    m_extractCount = extract;
    m_frequency = freq;
    m_realRows = qMax(1, getRowsByFrequency(freq) / qMax(1, extract));
    for (int i = 0; i < m_audioSelectedChannels.size(); ++i) {
        updateAudioDistanceLabel(i, m_audioSelectedChannels[i], freq, extract);
    }
    syncMonitorChannelRange(false);
    syncRmsPlotMode();
    QMessageBox::information(nullptr, "成功",
                             QString("抽取模式：%1（系数=%2）")
                                 .arg(m_extractCombo->currentText())
                                 .arg(extract));
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onPulseFrequencyButtonClicked()
{
    if (!m_lengthCombo) return;
    QString lengthText = m_lengthCombo->currentText();
    bool okData = false;
    int freq = m_lengthCombo->currentData(Qt::UserRole).toInt(&okData);
    if (!okData || freq <= 0) {
        freq = getPulseFreqByLength(lengthText);
    }
    if (freq <= 0) {
        QMessageBox::warning(nullptr, "输入无效", "光纤长度配置无效，请重新选择！");
        return;
    }

    // 更新参数
    ParameterManager::instance().setParameter("pulse_frequency", freq);
    m_frequency = freq;
    const int extractCount = ParameterManager::instance().getParameter("extract_count").toInt();
    m_extractCount = extractCount;
    m_realRows = qMax(1, getRowsByFrequency(freq) / qMax(1, extractCount));
    for (int i = 0; i < m_audioSelectedChannels.size(); ++i) {
        updateAudioDistanceLabel(i, m_audioSelectedChannels[i], freq, extractCount);
    }
    syncMonitorChannelRange(false);
    syncRmsPlotMode();
    syncFftPlotMode(freq);

    QMessageBox::information(nullptr, "成功", QString("监测长度%1, 脉冲频率已设为%2Hz").arg(lengthText).arg(freq));
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onDiffDistanceButtonClicked()
{
    if (!m_diffDistEdit) return;
    bool ok;
    int dist = m_diffDistEdit->text().toInt(&ok);
    if (!ok || dist < 1)
    {
        QMessageBox::warning(nullptr, "输入错误", "请输入大于等于 1 的整数！");
        return;
    }
    const int freq = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int maxDistance = qMax(1, getRowsByFrequency(freq) - 1);
    if (dist > maxDistance) {
        QMessageBox::warning(this,
                             QStringLiteral("输入错误"),
                             QStringLiteral("当前光纤长度下差分距离不能超过 %1。").arg(maxDistance));
        return;
    }
    ParameterManager::instance().setParameter("differential_distance", dist);
    m_diffDistEdit->setText(QString::number(dist));
    const double gaugeMeter = dist * MainWindowUtils::meterPerRawPoint(freq);
    syncMonitorChannelRange(false);
    if (m_dataModule) {
        m_dataModule->refreshRmsPlotFromCache();
    }
    QMessageBox::information(nullptr, "成功", QString("差分距离已设为 %1 m").arg(gaugeMeter, 0, 'f', 2));
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onDiffPhaseStateChanged(int state)
{
    const bool enabled = (state == Qt::Checked);
    ParameterManager::instance().setParameter("enable_diff_phase", enabled);

    if (ui && ui->statusBar) {
        ui->statusBar->showMessage(enabled ? QStringLiteral("已开启差分相位")
                                           : QStringLiteral("已关闭差分相位"),
                                   2000);
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onStartSaveButtonClicked()
{
    if (!m_startSaveEdit) return;
    bool ok = false;
    int val = m_startSaveEdit->text().trimmed().toInt(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, "输入错误", "请输入有效的起始通道！");
        return;
    }
    const int freq = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int extract = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    const int maxStartChannel = getMaxSaveStartChannel(freq, extract);
    const int maxEndChannel = getMaxSaveEndChannel(freq, extract);
    const int startChannel = qBound(0, val, maxStartChannel);
    int endChannel = qBound(1, ParameterManager::instance().getParameter("end_save").toInt(), maxEndChannel);
    if (startChannel >= endChannel) {
        endChannel = qMin(maxEndChannel, startChannel + 1);
        ParameterManager::instance().setParameter("end_save", endChannel);
        if (m_endSaveEdit) m_endSaveEdit->setText(QString::number(endChannel));
        m_endChannelUserEdited = (endChannel != maxEndChannel);
    }
    ParameterManager::instance().setParameter("start_save", startChannel);
    m_startSaveEdit->setText(QString::number(startChannel));
    syncMonitorChannelRange(false);
    if (ui && ui->statusBar) {
        ui->statusBar->showMessage(QString("监测起始通道已设为 %1").arg(startChannel), 2500);
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onEndSaveButtonClicked()
{
    if (!m_endSaveEdit) return;
    bool ok = false;
    int val = m_endSaveEdit->text().trimmed().toInt(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, "输入错误", "请输入有效的结束通道！");
        return;
    }
    const int freq = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int extract = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    const int maxStartChannel = getMaxSaveStartChannel(freq, extract);
    const int maxEndChannel = getMaxSaveEndChannel(freq, extract);
    const int startChannel = qBound(
        0,
        ParameterManager::instance().getParameter("start_save").toInt(),
        maxStartChannel);
    const int minEndChannel = qMin(maxEndChannel, startChannel + 1);
    const int endChannel = qBound(minEndChannel, val, maxEndChannel);
    ParameterManager::instance().setParameter("end_save", endChannel);
    m_endChannelUserEdited = (endChannel != maxEndChannel);
    m_endSaveEdit->setText(QString::number(endChannel));
    syncMonitorChannelRange(false);
    QMessageBox::information(nullptr, "成功", QString("监测结束通道已设为 %1").arg(endChannel));
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onSaveOnlyButtonClicked()
{
    if (m_saveAllActive || m_saveAllPending) {
        if (ui && ui->statusBar) {
            ui->statusBar->showMessage("全部保存进行中，请先结束保存全部。", 2500);
        }
        return;
    }
    if (m_singleSaveActive || m_pendingSavePanel >= 0) return;
    m_pendingSavePanel = 0;
    updateSingleSaveButtons();
    ParameterManager::instance().setParameter("singleSaveRow", m_audioSelectedChannels.value(0, 0));
    emit save_only();
}
/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onSaveAllButtonClicked()
{
    if (m_saveAllActive || m_saveAllPending) return;
    if (m_singleSaveActive || m_pendingSavePanel >= 0) {
        if (ui && ui->statusBar) {
            ui->statusBar->showMessage("单点保存进行中，请先结束保存。", 2500);
        }
        return;
    }
    m_saveAllPending = true;
    updateSaveAllUiState();
    emit save_all();
}
/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onFilterStateChanged(int state)
{
    const bool isChecked = (state == Qt::Checked);
    ParameterManager::instance().setParameter("enable_filter", isChecked);
    emit is_filter(isChecked);// 转发过滤器状态信号
    on_filter_stateChanged(state); // 同步刷新RMS/应变图与音频单位
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onLengthComboIndexChanged(int index)
{
    Q_UNUSED(index);
    // 仅记录日志，无核心逻辑
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onHighPassConfirmButtonClicked()
{
    if (!m_highPassCutoffEdit) return;

    bool ok = false;
    const double cutoffHz = m_highPassCutoffEdit->text().trimmed().toDouble(&ok);
    if (!ok || cutoffHz <= 0.0) {
        QMessageBox::warning(this, QStringLiteral("输入错误"), QStringLiteral("请输入大于 0 的高通截止频率(Hz)。"));
        return;
    }

    const int frequency = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const double nyquist = static_cast<double>(frequency) * 0.5;
    if (cutoffHz >= nyquist) {
        QMessageBox::warning(this,
                             QStringLiteral("参数过大"),
                             QStringLiteral("高通截止频率必须小于奈奎斯特频率 %1 Hz。").arg(nyquist, 0, 'f', 2));
        return;
    }

    ParameterManager::instance().setParameter("high_pass_cutoff_hz", cutoffHz);
    m_highPassCutoffEdit->setText(QString::number(cutoffHz, 'f', cutoffHz < 10.0 ? 2 : 1));
    if (ui && ui->statusBar) {
        ui->statusBar->showMessage(QStringLiteral("高通滤波器已更新，当前截止频率 %1 Hz").arg(cutoffHz, 0, 'f', 2), 3000);
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onSaveConfigClicked()
{
    QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存配置"),
        QDir::home().filePath(QStringLiteral("das_config.txt")),
        QStringLiteral("Text Files (*.txt);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }
    if (!filePath.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
        filePath += QStringLiteral(".txt");
    }

    QString errorMessage;
    if (!saveConfigToTextFile(filePath, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), errorMessage);
        return;
    }

    if (ui && ui->statusBar) {
        ui->statusBar->showMessage(QStringLiteral("配置已保存到 %1").arg(filePath), 4000);
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onLoadConfigClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("读取配置"),
        QDir::homePath(),
        QStringLiteral("Text Files (*.txt);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!loadConfigFromTextFile(filePath, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("读取失败"), errorMessage);
        return;
    }

    if (ui && ui->statusBar) {
        ui->statusBar->showMessage(QStringLiteral("配置已从 %1 读取").arg(filePath), 4000);
    }
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::setupAudioPanels()
{
    if (!ui || !ui->verticalLayout_2 || !ui->widget) return;

    ui->verticalLayout_2->removeWidget(ui->widget);
    ui->widget->hide();

    QWidget *audioContainer = new QWidget(ui->centralWidget);
    audioContainer->setObjectName("audioMultiContainer");
    QVBoxLayout *containerLayout = new QVBoxLayout(audioContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(6);

    QWidget *viewerToolbar = new QWidget(audioContainer);
    viewerToolbar->setObjectName("savedDataToolbar");
    QHBoxLayout *viewerToolbarLayout = new QHBoxLayout(viewerToolbar);
    viewerToolbarLayout->setContentsMargins(0, 0, 0, 0);
    viewerToolbarLayout->setSpacing(8);

    m_viewSavedSingleButton = new QPushButton(QStringLiteral("查看单点"), viewerToolbar);
    m_viewSavedAllButton = new QPushButton(QStringLiteral("查看保存全部"), viewerToolbar);
    m_viewSavedSingleButton->setProperty("savedDataAction", true);
    m_viewSavedAllButton->setProperty("savedDataAction", true);
    m_viewSavedSingleButton->setFixedHeight(scaleUiValue(30));
    m_viewSavedAllButton->setFixedHeight(scaleUiValue(30));
    m_viewSavedSingleButton->setMinimumWidth(scaleUiValue(110));
    m_viewSavedAllButton->setMinimumWidth(scaleUiValue(128));

    viewerToolbarLayout->addWidget(m_viewSavedSingleButton, 0, Qt::AlignLeft);
    viewerToolbarLayout->addWidget(m_viewSavedAllButton, 0, Qt::AlignLeft);
    viewerToolbarLayout->addStretch(1);
    containerLayout->addWidget(viewerToolbar);

    connect(m_viewSavedSingleButton, &QPushButton::clicked, this, &MainWindow::onViewSavedSingleDataClicked);
    connect(m_viewSavedAllButton, &QPushButton::clicked, this, &MainWindow::onViewSavedAllDataClicked);

    m_audioPlots.clear();
    m_audioChannelEdits.clear();
    m_audioChannelButtons.clear();
    m_audioSaveButtons.clear();
    m_audioEndSaveButtons.clear();
    m_audioResetButtons.clear();
    m_audioDistanceLabels.clear();
    m_audioUseBlue = QVector<bool>(2, true);

    for (int i = 0; i < 2; ++i) {
        QFrame *card = new QFrame(audioContainer);
        card->setObjectName("audioCard");
        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(6, 6, 6, 6);
        cardLayout->setSpacing(5);

        QWidget *headerRow = new QWidget(card);
        QHBoxLayout *headerLayout = new QHBoxLayout(headerRow);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(6);

        QLabel *panelBadge = new QLabel(QString("通道面板 %1").arg(i + 1), headerRow);
        panelBadge->setProperty("audioPanelBadge", true);
        panelBadge->setMinimumWidth(scaleUiValue(112));

        QLabel *headerTitle = new QLabel(QString("实时波形图 通道%1").arg(i + 1), headerRow);
        headerTitle->setProperty("audioHeaderTitle", true);
        headerTitle->setAlignment(Qt::AlignCenter);
        headerTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QFrame *controlDock = new QFrame(headerRow);
        controlDock->setObjectName(QString("audioControlDock%1").arg(i + 1));
        QHBoxLayout *dockLayout = new QHBoxLayout(controlDock);
        dockLayout->setContentsMargins(6, 3, 6, 3);
        dockLayout->setSpacing(5);

        QLabel *meterLabel = new QLabel(QString("通道 %1 | 0.00 m").arg(i), controlDock);
        meterLabel->setObjectName(QString("audioMeterLabel%1").arg(i + 1));
        meterLabel->setProperty("audioMeter", true);
        meterLabel->setAlignment(Qt::AlignVCenter);
        meterLabel->setMinimumWidth(scaleUiValue(220));

        QLabel *floatingLabel = new QLabel("通道", controlDock);
        floatingLabel->setObjectName("audioFloatingLabel");

        QLineEdit *channelEdit = new QLineEdit(controlDock);
        channelEdit->setObjectName(QString("audioChannelEdit%1").arg(i + 1));
        channelEdit->setFixedWidth(scaleUiValue(66));
        channelEdit->setText(QString::number(m_audioSelectedChannels.value(i, 0)));
        channelEdit->setAlignment(Qt::AlignCenter);
        channelEdit->setValidator(new QIntValidator(0, 999999, channelEdit));

        QPushButton *confirmBtn = new QPushButton("确定", controlDock);
        confirmBtn->setObjectName(QString("audioConfirmBtn%1").arg(i + 1));
        confirmBtn->setFixedWidth(scaleUiValue(46));

        QPushButton *saveBtn = new QPushButton("保存单点", controlDock);
        saveBtn->setObjectName(QString("audioSavePointBtn%1").arg(i + 1));
        saveBtn->setFixedWidth(scaleUiValue(72));

        QPushButton *endSaveBtn = new QPushButton("结束保存", controlDock);
        endSaveBtn->setObjectName(QString("audioEndSavePointBtn%1").arg(i + 1));
        endSaveBtn->setFixedWidth(scaleUiValue(72));

        QPushButton *resetBtn = new QPushButton("初始化", controlDock);
        resetBtn->setObjectName(QString("audioResetBtn%1").arg(i + 1));
        resetBtn->setFixedWidth(scaleUiValue(62));

        dockLayout->addWidget(meterLabel);
        dockLayout->addWidget(floatingLabel);
        dockLayout->addWidget(channelEdit);
        dockLayout->addWidget(confirmBtn);
        dockLayout->addWidget(saveBtn);
        dockLayout->addWidget(endSaveBtn);
        dockLayout->addWidget(resetBtn);

        headerLayout->addWidget(panelBadge, 0, Qt::AlignLeft | Qt::AlignVCenter);
        headerLayout->addWidget(headerTitle, 1);
        headerLayout->addWidget(controlDock, 0, Qt::AlignRight | Qt::AlignVCenter);

        QCustomPlot *plot = new QCustomPlot(card);
        plot->setMinimumHeight(scaleUiValue(170));

        cardLayout->addWidget(headerRow);
        cardLayout->addWidget(plot);
        containerLayout->addWidget(card);

        m_audioPlots.append(plot);
        m_audioChannelEdits.append(channelEdit);
        m_audioChannelButtons.append(confirmBtn);
        m_audioSaveButtons.append(saveBtn);
        m_audioEndSaveButtons.append(endSaveBtn);
        m_audioResetButtons.append(resetBtn);
        m_audioDistanceLabels.append(meterLabel);

        connect(confirmBtn, &QPushButton::clicked, this, [this, i]() {
            applyAudioChannelSelection(i);
        });
        connect(channelEdit, &QLineEdit::returnPressed, this, [this, i]() {
            applyAudioChannelSelection(i);
        });
        connect(saveBtn, &QPushButton::clicked, this, [this, i]() {
            if (m_singleSaveActive || m_pendingSavePanel >= 0 || m_saveAllActive || m_saveAllPending) {
                if (ui && ui->statusBar) {
                    const QString msg = (m_saveAllActive || m_saveAllPending)
                                            ? QStringLiteral("全部保存进行中，请先结束保存全部。")
                                            : QStringLiteral("单点保存进行中，请先结束保存。");
                    ui->statusBar->showMessage(msg, 2500);
                }
                return;
            }
            bool ok = false;
            const int channel = m_audioChannelEdits[i]->text().toInt(&ok);
            if (!ok || channel < 0) {
                QMessageBox::warning(this, "输入错误", "通道必须是非负整数。");
                return;
            }
            m_pendingSavePanel = i;
            updateSingleSaveButtons();
            applyAudioChannelSelection(i);
            ParameterManager::instance().setParameter("singleSaveRow", channel);
            ParameterManager::instance().setParameter("monitorPosition", channel);
            emit save_only();
            if (ui && ui->statusBar) {
                ui->statusBar->showMessage(QString("通道%1正在准备单点保存...").arg(i + 1), 2500);
            }
        });
        connect(endSaveBtn, &QPushButton::clicked, this, [this, i]() {
            Q_UNUSED(i);
            if (!m_singleSaveActive && m_pendingSavePanel < 0) return;
            emit end_save();
            if (ui && ui->statusBar) {
                ui->statusBar->showMessage("结束单点保存", 2500);
            }
        });
        connect(resetBtn, &QPushButton::clicked, this, [this, i]() {
            resetAudioPlotView(i);
        });
    }

    ui->verticalLayout_2->insertWidget(0, audioContainer);

    for (int i = 0; i < m_audioSelectedChannels.size(); ++i) {
        applyAudioChannelSelection(i);
    }

    updateSingleSaveButtons();
    updateOverlayButtonsGeometry();
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::applyAudioChannelSelection(int panelIndex)
{
    if (panelIndex < 0 || panelIndex >= m_audioChannelEdits.size()) return;
    bool ok = false;
    int channel = m_audioChannelEdits[panelIndex]->text().toInt(&ok);
    if (!ok || channel < 0) {
        QMessageBox::warning(this, "输入错误", "通道必须是非负整数。");
        return;
    }
    const int freq = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int extractCount = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    const int maxChannel = getMaxMonitorChannel(freq, extractCount);
    channel = qBound(0, channel, maxChannel);
    m_audioChannelEdits[panelIndex]->setText(QString::number(channel));

    const int prevChannel = m_audioSelectedChannels.value(panelIndex, channel);
    if (channel != prevChannel && panelIndex < m_audioUseBlue.size()) {
        if (panelIndex < m_audioPlots.size() && m_audioPlots[panelIndex] && m_audioPlots[panelIndex]->graphCount() > 1) {
            QPen waveformPen(QColor(255, 128, 0), 1.0);
            waveformPen.setCapStyle(Qt::RoundCap);
            waveformPen.setJoinStyle(Qt::RoundJoin);
            m_audioPlots[panelIndex]->graph(1)->setPen(waveformPen);
            m_audioPlots[panelIndex]->graph(1)->setLineStyle(QCPGraph::lsLine);
            m_audioPlots[panelIndex]->graph(1)->setScatterStyle(QCPScatterStyle::ssNone);
            m_audioPlots[panelIndex]->graph(1)->setAdaptiveSampling(false);
            m_audioPlots[panelIndex]->graph(1)->setAntialiased(false);
        }
        if (m_dataModule) {
            m_dataModule->clearAudioChannelData(panelIndex);
        } else if (panelIndex < m_audioPlots.size() && m_audioPlots[panelIndex]) {
            if (m_audioPlots[panelIndex]->graphCount() > 1 && m_audioPlots[panelIndex]->graph(1) && m_audioPlots[panelIndex]->graph(1)->data()) {
                m_audioPlots[panelIndex]->graph(1)->data()->clear();
            }
            m_audioPlots[panelIndex]->xAxis->setRange(0, 5);
            m_audioPlots[panelIndex]->replot(QCustomPlot::rpQueuedReplot);
        }
    }

    m_audioSelectedChannels[panelIndex] = channel;
    ParameterManager::instance().setParameter(QString("monitorPosition%1").arg(panelIndex + 1), channel);
    if (panelIndex == 0) {
        ParameterManager::instance().setParameter("monitorPosition", channel);
    }
    updateAudioDistanceLabel(panelIndex, channel, freq, extractCount);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onViewSavedSingleDataClicked()
{
    openSavedDataViewer(false);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onViewSavedAllDataClicked()
{
    openSavedDataViewer(true);
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::openSavedDataViewer(bool loadAllData)
{
    QString selectedPath;
    QString selectedBinPath;
    if (loadAllData) {
        selectedPath = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("选择保存全部数据文件夹"),
            QDir::homePath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (selectedPath.isEmpty()) return;

        selectedBinPath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择该文件夹中的 bin 文件"),
            selectedPath,
            QStringLiteral("BIN Files (*.bin)"));
        if (selectedBinPath.isEmpty()) return;

        if (QFileInfo(selectedBinPath).absolutePath() != QFileInfo(selectedPath).absoluteFilePath()) {
            QMessageBox::warning(this,
                                 QStringLiteral("选择失败"),
                                 QStringLiteral("请选择当前保存全部文件夹中的 bin 文件。"));
            return;
        }
    } else {
        selectedPath = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("选择单点保存数据文件夹"),
            QDir::homePath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    }
    if (selectedPath.isEmpty()) return;

    SavedDataViewer *viewer = new SavedDataViewer(this);
    QString errorMessage;
    const bool ok = loadAllData
                        ? viewer->loadSaveAllBinFile(selectedPath, selectedBinPath, &errorMessage)
                        : viewer->loadSinglePointFolder(selectedPath, &errorMessage);
    if (!ok) {
        viewer->deleteLater();
        QMessageBox::warning(this,
                             QStringLiteral("读取失败"),
                             errorMessage.isEmpty()
                                 ? QStringLiteral("无法读取所选保存数据。")
                                 : errorMessage);
        return;
    }

    viewer->show();
    viewer->raise();
    viewer->activateWindow();
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::syncConfigUiFromParameters()
{
    const int frequency = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int extractCount = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    const int maxStartChannel = getMaxSaveStartChannel(frequency, extractCount);
    const int maxMonitorChannel = getMaxMonitorChannel(frequency, extractCount);
    const int maxEndChannel = getMaxSaveEndChannel(frequency, extractCount);

    if (ui && ui->length_selection_comboBox) {
        const int freqIndex = ui->length_selection_comboBox->findData(frequency);
        if (freqIndex >= 0) {
            ui->length_selection_comboBox->setCurrentIndex(freqIndex);
        }
    }
    if (ui && ui->extract_edit) {
        const int extractIndex = ui->extract_edit->findData(extractCount);
        if (extractIndex >= 0) {
            ui->extract_edit->setCurrentIndex(extractIndex);
        }
    }

    if (m_diffDistEdit) {
        m_diffDistEdit->setText(QString::number(ParameterManager::instance().getParameter("differential_distance").toInt()));
    }

    const int startChannel = qBound(0,
                                    ParameterManager::instance().getParameter("start_save").toInt(),
                                    maxStartChannel);
    const int minEndChannel = qMin(maxEndChannel, startChannel + 1);
    const int endChannel = qBound(minEndChannel,
                                  ParameterManager::instance().getParameter("end_save").toInt(),
                                  maxEndChannel);
    m_endChannelUserEdited = (endChannel != maxEndChannel);
    if (m_startSaveEdit) m_startSaveEdit->setText(QString::number(startChannel));
    if (m_endSaveEdit) m_endSaveEdit->setText(QString::number(endChannel));

    const int monitor1 = qBound(0,
                                ParameterManager::instance().getParameter("monitorPosition1").toInt(),
                                maxMonitorChannel);
    const int monitor2 = qBound(0,
                                ParameterManager::instance().getParameter("monitorPosition2").toInt(),
                                maxMonitorChannel);
    if (!m_audioSelectedChannels.isEmpty()) {
        m_audioSelectedChannels[0] = monitor1;
    }
    if (m_audioSelectedChannels.size() > 1) {
        m_audioSelectedChannels[1] = monitor2;
    }
    if (!m_audioChannelEdits.isEmpty() && m_audioChannelEdits[0]) {
        m_audioChannelEdits[0]->setText(QString::number(monitor1));
    }
    if (m_audioChannelEdits.size() > 1 && m_audioChannelEdits[1]) {
        m_audioChannelEdits[1]->setText(QString::number(monitor2));
    }

    if (m_filterBox) {
        QSignalBlocker blocker(m_filterBox);
        m_filterBox->setChecked(ParameterManager::instance().getParameter("enable_filter").toBool());
    }
    onFilterStateChanged(m_filterBox && m_filterBox->isChecked() ? Qt::Checked : Qt::Unchecked);

    if (m_diffPhaseCheckBox) {
        QSignalBlocker blocker(m_diffPhaseCheckBox);
        m_diffPhaseCheckBox->setChecked(ParameterManager::instance().getParameter("enable_diff_phase").toBool());
    }
    onDiffPhaseStateChanged(m_diffPhaseCheckBox && m_diffPhaseCheckBox->isChecked() ? Qt::Checked : Qt::Unchecked);

    const double cutoffHz = ParameterManager::instance().getParameter("high_pass_cutoff_hz").toDouble();
    if (m_highPassCutoffEdit) {
        m_highPassCutoffEdit->setText(QString::number(cutoffHz > 0.0 ? cutoffHz : 1.0, 'f', 2));
    }

    m_frequency = frequency;
    m_extractCount = extractCount;
    m_realRows = qMax(1, getRowsByFrequency(frequency) / qMax(1, extractCount));
    syncMonitorChannelRange(false);
    for (int i = 0; i < m_audioSelectedChannels.size(); ++i) {
        if (i < m_audioChannelEdits.size() && m_audioChannelEdits[i]) {
            m_audioChannelEdits[i]->setText(QString::number(m_audioSelectedChannels[i]));
        }
        applyAudioChannelSelection(i);
    }
    updateMonitorScaleLabels();
    syncRmsPlotMode();
    syncFftPlotMode(frequency);
}

/**
 * @brief 执行本功能的数据读写步骤，并沿用现有的错误处理与路径约定。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
bool MainWindow::saveConfigToTextFile(const QString& filePath, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入配置文件：%1").arg(file.errorString());
        }
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "# 啁啾扫频型分布式光纤测井设备 configuration\n";
    out << "# key=value\n";
    for (const QString &key : kConfigKeys) {
        const QVariant value = ParameterManager::instance().getParameter(key);
        if (!value.isValid()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("当前生效参数缺少配置项：%1").arg(key);
            }
            file.close();
            return false;
        }
        out << key << "=" << value.toString() << "\n";
    }

    out.flush();
    if (out.status() != QTextStream::Ok || !file.flush()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置文件写入失败：%1").arg(file.errorString());
        }
        file.close();
        return false;
    }

    file.close();
    return true;
}

/**
 * @brief 执行本功能的数据读写步骤，并沿用现有的错误处理与路径约定。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
bool MainWindow::loadConfigFromTextFile(const QString& filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取配置文件：%1").arg(file.errorString());
        }
        return false;
    }

    QMap<QString, QString> configMap;
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    int lineNumber = 0;
    while (!in.atEnd()) {
        ++lineNumber;
        const QString rawLine = in.readLine().trimmed();
        if (rawLine.isEmpty() || rawLine.startsWith('#')) {
            continue;
        }
        const int splitPos = rawLine.indexOf('=');
        if (splitPos <= 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("配置文件第 %1 行格式错误，应为 key=value。").arg(lineNumber);
            }
            return false;
        }
        const QString key = rawLine.left(splitPos).trimmed();
        const QString value = rawLine.mid(splitPos + 1).trimmed();
        configMap.insert(key, value);
    }
    file.close();

    bool hasKnownKey = false;
    for (const QString &key : kConfigKeys) {
        if (configMap.contains(key)) {
            hasKnownKey = true;
            break;
        }
    }
    if (!hasKnownKey) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("文件中没有可识别的设备配置项。");
        }
        return false;
    }

    QString invalidKey;
    auto loadInt = [&](const QString &key, int fallback) -> int {
        if (!configMap.contains(key)) return fallback;
        bool ok = false;
        const int value = configMap.value(key).toInt(&ok);
        if (!ok && invalidKey.isEmpty()) invalidKey = key;
        return ok ? value : fallback;
    };
    auto loadDouble = [&](const QString &key, double fallback) -> double {
        if (!configMap.contains(key)) return fallback;
        bool ok = false;
        const double value = configMap.value(key).toDouble(&ok);
        if (!ok && invalidKey.isEmpty()) invalidKey = key;
        return ok ? value : fallback;
    };
    auto loadBool = [&](const QString &key, bool fallback) -> bool {
        if (!configMap.contains(key)) return fallback;
        bool ok = false;
        const bool value = parseBoolText(configMap.value(key), &ok);
        if (!ok && invalidKey.isEmpty()) invalidKey = key;
        return ok ? value : fallback;
    };

    const int currentFrequency = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int frequency = loadInt(QStringLiteral("pulse_frequency"), currentFrequency);
    const int extractCount = loadInt(QStringLiteral("extract_count"),
                                     ParameterManager::instance().getParameter("extract_count").toInt());
    const int startChannel = loadInt(QStringLiteral("start_save"),
                                     ParameterManager::instance().getParameter("start_save").toInt());
    const int endChannel = loadInt(QStringLiteral("end_save"),
                                   ParameterManager::instance().getParameter("end_save").toInt());
    const int legacyMonitor = loadInt(QStringLiteral("monitorPosition"),
                                      ParameterManager::instance().getParameter("monitorPosition").toInt());
    const int monitor1 = loadInt(QStringLiteral("monitorPosition1"), legacyMonitor);
    const int monitor2 = loadInt(QStringLiteral("monitorPosition2"),
                                 ParameterManager::instance().getParameter("monitorPosition2").toInt());
    const int singleSaveRow = loadInt(QStringLiteral("singleSaveRow"),
                                      ParameterManager::instance().getParameter("singleSaveRow").toInt());
    const int differentialDistance = loadInt(
        QStringLiteral("differential_distance"),
        ParameterManager::instance().getParameter("differential_distance").toInt());
    const bool diffPhaseEnabled = loadBool(
        QStringLiteral("enable_diff_phase"),
        ParameterManager::instance().getParameter("enable_diff_phase").toBool());
    const bool filterEnabled = loadBool(
        QStringLiteral("enable_filter"),
        ParameterManager::instance().getParameter("enable_filter").toBool());
    const double cutoffHz = loadDouble(QStringLiteral("high_pass_cutoff_hz"),
                                       ParameterManager::instance().getParameter("high_pass_cutoff_hz").toDouble());
    const int numThreads = loadInt(QStringLiteral("numThreads"),
                                   ParameterManager::instance().getParameter("numThreads").toInt());

    if (!invalidKey.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置项 %1 的值格式无效。").arg(invalidKey);
        }
        return false;
    }

    if (!isSupportedPulseFrequency(frequency)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的脉冲频率 %1 Hz 不受当前设备支持。").arg(frequency);
        }
        return false;
    }
    if (!isSupportedExtractCount(extractCount)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的抽取系数 %1 不受当前界面支持。").arg(extractCount);
        }
        return false;
    }

    const int rawRows = getRowsByFrequency(frequency);
    const int maxEndChannel = getMaxSaveEndChannel(frequency, extractCount);
    if (differentialDistance < 1 || differentialDistance >= rawRows) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的差分距离必须在 1-%1 之间。").arg(rawRows - 1);
        }
        return false;
    }

    const int maxStartChannel =
        qMax(0, (rawRows - differentialDistance - 1) / qMax(1, extractCount));
    if (startChannel < 0 || startChannel > maxStartChannel ||
        endChannel <= startChannel || endChannel > maxEndChannel) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的监测通道范围无效：起始需为 0-%1，结束需大于起始且不超过 %2。")
                                .arg(maxStartChannel)
                                .arg(maxEndChannel);
        }
        return false;
    }

    const int outputRowCount = getOutputRowCount(frequency,
                                                  extractCount,
                                                  startChannel,
                                                  endChannel,
                                                  differentialDistance);
    if (outputRowCount <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的通道范围无法产生有效的差分数据。");
        }
        return false;
    }
    const int maxMonitorChannel = outputRowCount - 1;
    if (monitor1 < 0 || monitor1 > maxMonitorChannel ||
        monitor2 < 0 || monitor2 > maxMonitorChannel ||
        singleSaveRow < 0 || singleSaveRow > maxMonitorChannel) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的监听或单点保存通道超出 0-%1。").arg(maxMonitorChannel);
        }
        return false;
    }
    if (!std::isfinite(cutoffHz) ||
        cutoffHz <= 0.0 ||
        cutoffHz >= static_cast<double>(frequency) * 0.5) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的高通截止频率必须大于 0 且小于 %1 Hz。")
                                .arg(static_cast<double>(frequency) * 0.5, 0, 'g', 8);
        }
        return false;
    }
    if (numThreads < 1 || numThreads > 64) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置中的线程数必须在 1-64 之间。");
        }
        return false;
    }

    ParameterManager::instance().setParameter("extract_count", extractCount);
    ParameterManager::instance().setParameter("start_save", startChannel);
    ParameterManager::instance().setParameter("end_save", endChannel);
    ParameterManager::instance().setParameter("monitorPosition", monitor1);
    ParameterManager::instance().setParameter("monitorPosition1", monitor1);
    ParameterManager::instance().setParameter("monitorPosition2", monitor2);
    ParameterManager::instance().setParameter("singleSaveRow", singleSaveRow);
    ParameterManager::instance().setParameter("differential_distance", differentialDistance);
    ParameterManager::instance().setParameter("enable_diff_phase", diffPhaseEnabled);
    ParameterManager::instance().setParameter("enable_filter", filterEnabled);
    ParameterManager::instance().setParameter("high_pass_cutoff_hz", cutoffHz);
    ParameterManager::instance().setParameter("numThreads", numThreads);
    ParameterManager::instance().setParameter("pulse_frequency", frequency);

    syncConfigUiFromParameters();
    return true;
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
double MainWindow::computeUiScale() const
{
    QScreen *targetScreen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    if (!targetScreen) {
        return 1.0;
    }

    const QRect available = targetScreen->availableGeometry();
    const double widthScale = static_cast<double>(available.width()) / 1700.0;
    const double heightScale = static_cast<double>(available.height()) / 980.0;
    return qBound(0.78, qMin(widthScale, heightScale), 1.0);
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
int MainWindow::scaleUiValue(int value) const
{
    return qMax(1, qRound(value * m_uiScale));
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateAudioDistanceLabel(int panelIndex, int channelIndex, int frequency, int extractCount)
{
    if (panelIndex < 0 || panelIndex >= m_audioDistanceLabels.size()) return;
    const double meterPerChannel = MainWindowUtils::meterPerExtractedChannel(frequency, extractCount);
    const int startChannel =
        qMax(0, ParameterManager::instance().getParameter("start_save").toInt());
    const int absoluteChannel = startChannel + qMax(0, channelIndex);
    const double meter =
        MainWindowUtils::channelToMeter(absoluteChannel, frequency, extractCount);
    m_audioDistanceLabels[panelIndex]->setText(
        QString("通道 %1 | %2 m (%3m/通道)")
            .arg(channelIndex)
            .arg(meter, 0, 'f', 2)
            .arg(meterPerChannel, 0, 'f', 2));
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateMonitorScaleLabels()
{
    if (!ui) return;

    const int freq = ParameterManager::instance().getParameter("pulse_frequency").toInt();
    const int extract = ParameterManager::instance().getParameter("extract_count").toInt();
    const double meterPerChannel = MainWindowUtils::meterPerExtractedChannel(freq, extract);
    const QString unitText = QString("%1m/通道").arg(meterPerChannel, 0, 'f', 2);

    if (ui->label_7) ui->label_7->setText(QStringLiteral("监测起始通道:"));
    if (ui->label_8) ui->label_8->setText(QStringLiteral("监测结束通道:"));
    if (ui->label_9) ui->label_9->setText(unitText);
    if (ui->label_10) ui->label_10->setText(unitText);
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateRmsXAxisModeButtonText()
{
    if (!m_rmsXAxisModeButton) return;
    m_rmsXAxisModeButton->setText(m_rmsXAxisChannelMode ? QStringLiteral("X轴: 通道") : QStringLiteral("X轴: 米"));
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateRmsModeButtonText()
{
    if (!m_rmsModeButton) return;
    m_rmsModeButton->setText(m_rmsLineMode ? QStringLiteral("切换瀑布图") : QStringLiteral("切换单帧图"));
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateRmsPauseButtonText()
{
    if (!m_rmsPauseButton) return;
    m_rmsPauseButton->setText(m_rmsPaused ? QStringLiteral("继续刷新") : QStringLiteral("暂停刷新"));
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateSingleSaveButtons()
{
    const bool busy = m_singleSaveActive || (m_pendingSavePanel >= 0);
    const int highlightPanel = m_singleSaveActive ? m_activeSavePanel : m_pendingSavePanel;

    for (int i = 0; i < m_audioSaveButtons.size(); ++i) {
        QPushButton *saveBtn = m_audioSaveButtons[i];
        QPushButton *endBtn = (i < m_audioEndSaveButtons.size()) ? m_audioEndSaveButtons[i] : nullptr;
        if (!saveBtn) continue;

        const bool highlighted = (i == highlightPanel) && busy;
        saveBtn->setProperty("saving", highlighted);
        if (highlighted) {
            saveBtn->setText(m_singleSaveActive ? QStringLiteral("正在保存") : QStringLiteral("准备保存"));
            saveBtn->setEnabled(false);
        } else {
            saveBtn->setText(QStringLiteral("保存单点"));
            saveBtn->setEnabled(!busy);
        }

        if (endBtn) {
            endBtn->setEnabled(m_singleSaveActive);
        }

        saveBtn->style()->unpolish(saveBtn);
        saveBtn->style()->polish(saveBtn);
        saveBtn->update();
    }

    // 单点保存期间锁定通道输入，防止保存过程中切换通道导致行号漂移。
    for (QLineEdit *edit : m_audioChannelEdits) {
        if (edit) edit->setEnabled(!busy);
    }
    for (QPushButton *btn : m_audioChannelButtons) {
        if (btn) btn->setEnabled(!busy);
    }
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateSaveAllUiState()
{
    const bool busy = m_saveAllActive || m_saveAllPending;

    if (ui && ui->save_all_button) {
        ui->save_all_button->setEnabled(!busy);
    }
    if (ui && ui->end_save_all_button) {
        ui->end_save_all_button->setEnabled(m_saveAllActive);
    }

    const QString dotColor = m_saveAllActive ? QStringLiteral("#20ad67") : QStringLiteral("#d9534f");
    const QString borderColor = m_saveAllActive ? QStringLiteral("#0f7b47") : QStringLiteral("#a94343");
    const QString dotStyle = QString(
                                 "background:%1;"
                                 "border:1px solid %2;"
                                 "border-radius:6px;"
                                 "min-width:12px;max-width:12px;"
                                 "min-height:12px;max-height:12px;")
                                 .arg(dotColor, borderColor);

    if (m_saveAllStateDot) {
        m_saveAllStateDot->setStyleSheet(dotStyle);
        m_saveAllStateDot->setToolTip(m_saveAllActive
                                          ? QStringLiteral("保存全部正在进行")
                                          : QStringLiteral("保存全部未进行"));
    }
    if (m_saveAllStateLabel) {
        m_saveAllStateLabel->setText(m_saveAllActive
                                         ? QStringLiteral("保存全部中")
                                         : QStringLiteral("未保存"));
        m_saveAllStateLabel->setStyleSheet(QString("color:%1; font-weight:600;")
                                               .arg(m_saveAllActive ? "#0f7b47" : "#a94343"));
    }
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::applyCardShadow(QWidget *target, int blurRadius, int yOffset)
{
    if (!target) return;

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(target);
    shadow->setBlurRadius(blurRadius);
    shadow->setOffset(0, yOffset);
    shadow->setColor(QColor(18, 43, 73, 45));
    target->setGraphicsEffect(shadow);
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::applyUiTheme()
{
    if (!ui) return;

    m_uiScale = computeUiScale();

    QFont appFont("Microsoft YaHei UI", qMax(8.0, 9.0 * m_uiScale));
    setFont(appFont);
    if (ui->statusBar) {
        ui->statusBar->setSizeGripEnabled(false);
    }

    ui->gridLayout->setContentsMargins(scaleUiValue(10), scaleUiValue(10), scaleUiValue(10), scaleUiValue(10));
    ui->gridLayout->setHorizontalSpacing(scaleUiValue(8));
    ui->gridLayout->setVerticalSpacing(scaleUiValue(8));
    ui->horizontalLayout->setSpacing(scaleUiValue(10));
    ui->verticalLayout->setSpacing(scaleUiValue(10));
    ui->verticalLayout_2->setSpacing(scaleUiValue(10));
    ui->horizontalLayout_2->setContentsMargins(0, 0, 0, 0);

    const QString theme = R"(
QMainWindow {
    background: #f5f7fb;
}
QWidget#centralWidget {
    background: #ffffff;
}
QWidget#widget_5 {
    background: #ffffff;
    border: 1px solid #d9e2ec;
    border-radius: 12px;
}
QWidget#audioMultiContainer {
    background: transparent;
}
QFrame#audioCard {
    background: #ffffff;
    border: 1px solid #d6e1eb;
    border-radius: 10px;
}
QWidget#savedDataToolbar {
    background: transparent;
}
QPushButton[savedDataAction="true"] {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #edf6ff, stop:1 #d6e8fb);
    color: #103255;
    border: 1px solid #9eb9d6;
    border-radius: 9px;
    padding: 6px 14px;
    font: 700 10pt "Microsoft YaHei UI";
}
QPushButton[savedDataAction="true"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #dcecff);
    border-color: #6f97c0;
}
QPushButton[savedDataAction="true"]:pressed {
    background: #cfe2f7;
}
QLabel[audioPanelBadge="true"] {
    color: #111111;
    font-weight: 700;
    background: #eef5ff;
    border: 1px solid #bdd1ea;
    border-radius: 7px;
    padding: 2px 8px;
}
QLabel[audioHeaderTitle="true"] {
    color: #111111;
    font: 700 12pt "Microsoft YaHei UI";
    padding: 0 6px;
}
QLabel[audioMeter="true"] {
    color: #111111;
    font-weight: 600;
}
QCustomPlot {
    border: 1px solid #c3d2e4;
    border-radius: 10px;
    background-color: #ffffff;
}
QLabel {
    color: #111111;
    font: 9pt "Microsoft YaHei UI";
}
QLabel#pulse_frequency_label,
QLabel#extract_label,
QLabel#channel_label,
QLabel#channel_label_2,
QLabel#label_7,
QLabel#label_8,
QLabel#label_9,
QLabel#label_10 {
    color: #42566f;
    font-weight: 600;
}
QLabel#label,
QLabel#label_2,
QLabel#label_3,
QLabel#label_4,
QLabel#label_5,
QLabel#label_6,
QLabel#label_11,
QLabel#label_12,
QLabel#label_13,
QLabel#label_14 {
    color: #111111;
}
QLabel#timelabel1,
QLabel#timelabel2,
QLabel#timelabel3,
QLabel#timelabel4,
QLabel#timelabel5,
QLabel#timelabel6 {
    color: #1f6fd6;
    font-weight: 600;
}
QPushButton {
    background-color: #f0f6ff;
    color: #203246;
    border: 1px solid #afc7e2;
    border-radius: 6px;
    padding: 2px 10px;
    font: 9pt "Microsoft YaHei UI";
}
QWidget#widget_5 QPushButton {
    padding: 1px 6px;
    min-height: 24px;
    font: 8.5pt "Microsoft YaHei UI";
}
QPushButton:hover {
    background-color: #e4efff;
    border-color: #8db2de;
}
QPushButton:pressed {
    background-color: #d5e6ff;
}
QPushButton:disabled {
    background-color: #d7e1ee;
    color: #8ba0b8;
    border-color: #bacadb;
}
QPushButton#pushButton {
    background-color: #17955d;
    border: 1px solid #0e7447;
    color: white;
    min-width: 74px;
    min-height: 24px;
}
QPushButton#pushButton:hover {
    background-color: #14a465;
}
QPushButton#pushButton_2 {
    background-color: #d45a5a;
    border: 1px solid #ad4040;
    color: white;
    min-width: 74px;
    min-height: 24px;
}
QPushButton#pushButton_2:hover {
    background-color: #e06868;
}
QLineEdit,
QComboBox {
    background-color: #ffffff;
    color: #243548;
    border: 1px solid #b7cae0;
    border-radius: 6px;
    padding: 2px 6px;
}
QLineEdit:focus,
QComboBox:focus {
    border-color: #3e88eb;
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 20px;
    border-left: 1px solid #c8d6e8;
}
QComboBox::down-arrow {
    image: none;
    width: 0px;
    height: 0px;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 7px solid #3e5d80;
    margin-right: 6px;
}
QComboBox#length_selection_comboBox,
QComboBox#extract_edit {
    border: 2px solid #3f8ff2;
    padding-right: 26px;
    font-weight: 600;
    min-height: 24px;
}
QComboBox#length_selection_comboBox::drop-down,
QComboBox#extract_edit::drop-down {
    width: 24px;
    border-left: 1px solid #2f7eea;
    background: #2f7eea;
    border-top-right-radius: 5px;
    border-bottom-right-radius: 5px;
}
QComboBox#length_selection_comboBox::down-arrow,
QComboBox#extract_edit::down-arrow {
    border-top: 7px solid #ffffff;
}
QComboBox#length_selection_comboBox:hover,
QComboBox#extract_edit:hover {
    border-color: #2b78de;
}
QFrame#audioControlDock1,
QFrame#audioControlDock2 {
    background: #f8fbff;
    border: 1px solid #d4e0ec;
    border-radius: 8px;
}
QLabel#audioFloatingLabel {
    color: #111111;
    font-weight: 600;
}
QLineEdit#audioChannelEdit1,
QLineEdit#audioChannelEdit2 {
    background: #f7fbff;
    border: 1px solid #6d92be;
    color: #24364a;
    border-radius: 5px;
    min-height: 22px;
    font-weight: 600;
}
QLineEdit#audioChannelEdit1:focus,
QLineEdit#audioChannelEdit2:focus {
    border-color: #f28b33;
}
QPushButton#audioConfirmBtn1,
QPushButton#audioConfirmBtn2,
QPushButton#audioSavePointBtn1,
QPushButton#audioSavePointBtn2,
QPushButton#audioEndSavePointBtn1,
QPushButton#audioEndSavePointBtn2,
QPushButton#audioResetBtn1,
QPushButton#audioResetBtn2 {
    min-height: 22px;
    padding: 1px 6px;
    border: 1px solid #8caed2;
    background: #e8f2ff;
}
QPushButton#audioConfirmBtn1:hover,
QPushButton#audioConfirmBtn2:hover,
QPushButton#audioSavePointBtn1:hover,
QPushButton#audioSavePointBtn2:hover,
QPushButton#audioEndSavePointBtn1:hover,
QPushButton#audioEndSavePointBtn2:hover,
QPushButton#audioResetBtn1:hover,
QPushButton#audioResetBtn2:hover {
    background: #dcecff;
}
QPushButton#audioEndSavePointBtn1,
QPushButton#audioEndSavePointBtn2 {
    background: #fff0f0;
    border-color: #d6a0a0;
}
QPushButton#audioEndSavePointBtn1:hover,
QPushButton#audioEndSavePointBtn2:hover {
    background: #ffe3e3;
}
QPushButton[saving="true"] {
    background: #20ad67;
    border-color: #0f7b47;
    color: #ffffff;
    font-weight: 700;
}
QPushButton[saving="true"]:hover {
    background: #1aa45f;
}
QPushButton#fftModeToggleBtn,
QPushButton#rmsResetBtn,
QPushButton#rmsAxisModeBtn,
QPushButton#rmsModeToggleBtn,
QPushButton#rmsPauseBtn {
    min-height: 24px;
    padding: 1px 10px;
    background: rgba(232, 242, 255, 220);
    border: 1px solid #93b3d7;
    border-radius: 6px;
    color: #1e3550;
    font-weight: 600;
}
QPushButton#fftModeToggleBtn:hover,
QPushButton#rmsResetBtn:hover,
QPushButton#rmsAxisModeBtn:hover,
QPushButton#rmsModeToggleBtn:hover,
QPushButton#rmsPauseBtn:hover {
    background: rgba(220, 236, 255, 235);
}
QCheckBox {
    color: #111111;
    spacing: 6px;
}
QCheckBox#filter,
QCheckBox#diffPhaseCheckBox {
    font-weight: 600;
}
QStatusBar {
    background: #ffffff;
    color: #111111;
    border-top: 1px solid #c9d8ea;
}
QMenuBar, QToolBar {
    background: #ffffff;
    border: none;
}
QToolTip {
    background-color: #ffffff;
    color: #243548;
    border: 1px solid #b9cadb;
}
)";

    setStyleSheet(theme);

    if (ui->widget_5 && !m_diffPhaseCheckBox) {
        m_diffPhaseCheckBox = new QCheckBox(QStringLiteral("差分相位"), ui->widget_5);
        m_diffPhaseCheckBox->setObjectName("diffPhaseCheckBox");
        m_diffPhaseCheckBox->setToolTip(QStringLiteral("在时间解缠绕完成后，对每一行按相邻时间点做差分"));
        m_diffPhaseCheckBox->setChecked(ParameterManager::instance().getParameter("enable_diff_phase").toBool());
    }
    if (ui->widget_5 && !m_highPassLabel) {
        m_highPassLabel = new QLabel(QStringLiteral("高通截止(Hz)"), ui->widget_5);
        m_highPassLabel->setObjectName("highPassLabel");
    }
    if (ui->widget_5 && !m_highPassCutoffEdit) {
        m_highPassCutoffEdit = new QLineEdit(ui->widget_5);
        m_highPassCutoffEdit->setObjectName("highPassCutoffEdit");
        m_highPassCutoffEdit->setAlignment(Qt::AlignCenter);
        auto *validator = new QDoubleValidator(0.01, 1000000.0, 2, m_highPassCutoffEdit);
        validator->setNotation(QDoubleValidator::StandardNotation);
        m_highPassCutoffEdit->setValidator(validator);
        m_highPassCutoffEdit->setText(QStringLiteral("1.00"));
        m_highPassCutoffEdit->setToolTip(QStringLiteral("默认 1Hz，高于该频率的分量通过。"));
    }
    if (ui->widget_5 && !m_highPassConfirmButton) {
        m_highPassConfirmButton = new QPushButton(QStringLiteral("滤波确定"), ui->widget_5);
        m_highPassConfirmButton->setObjectName("highPassConfirmButton");
    }
    if (ui->widget_5 && !m_saveConfigButton) {
        m_saveConfigButton = new QPushButton(QStringLiteral("保存配置"), ui->widget_5);
        m_saveConfigButton->setObjectName("saveConfigButton");
    }
    if (ui->widget_5 && !m_loadConfigButton) {
        m_loadConfigButton = new QPushButton(QStringLiteral("读取配置"), ui->widget_5);
        m_loadConfigButton->setObjectName("loadConfigButton");
    }

    if (ui->pushButton) ui->pushButton->hide();
    if (ui->pushButton_2) ui->pushButton_2->hide();
    if (ui->save_single_button) ui->save_single_button->hide();
    if (ui->end_save_button) ui->end_save_button->hide();
    if (ui->channel_label) ui->channel_label->hide();
    if (ui->channel_label_edit) ui->channel_label_edit->hide();
    if (ui->channel_button) ui->channel_button->hide();
    if (ui->label_11) {
        ui->label_11->show();
        ui->label_11->setText(QStringLiteral("存储耗时："));
    }
    if (ui->label_13) {
        ui->label_13->show();
        ui->label_13->setText(QStringLiteral("0 ms"));
    }
    if (ui->horizontalLayout_2) {
        ui->horizontalLayout_2->setSpacing(0);
        for (int i = 0; i < ui->horizontalLayout_2->count(); ++i) {
            QLayoutItem *item = ui->horizontalLayout_2->itemAt(i);
            if (!item) continue;
            if (QSpacerItem *spacer = item->spacerItem()) {
                spacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
            }
        }
        ui->horizontalLayout_2->invalidate();
    }
    if (ui->verticalLayout_3) {
        ui->verticalLayout_3->setStretch(0, 1);
        ui->verticalLayout_3->setStretch(1, 0);
    }
    setupPlotToolButtons();
    updateOverlayButtonsGeometry();

    ui->widget_5->setMinimumHeight(scaleUiValue(286));

    // 控制区重新排布，避免文本拥挤
    ui->pulse_frequency_label->setGeometry(scaleUiValue(14), scaleUiValue(14), scaleUiValue(64), scaleUiValue(24));
    ui->length_selection_comboBox->setGeometry(scaleUiValue(82), scaleUiValue(14), scaleUiValue(120), scaleUiValue(24));
    ui->pulse_frequency_button->setGeometry(scaleUiValue(208), scaleUiValue(14), scaleUiValue(58), scaleUiValue(24));

    ui->extract_label->setGeometry(scaleUiValue(14), scaleUiValue(46), scaleUiValue(64), scaleUiValue(24));
    ui->extract_edit->setGeometry(scaleUiValue(82), scaleUiValue(46), scaleUiValue(120), scaleUiValue(24));
    ui->extract_button->setGeometry(scaleUiValue(208), scaleUiValue(46), scaleUiValue(58), scaleUiValue(24));

    ui->channel_label->setGeometry(scaleUiValue(14), scaleUiValue(78), scaleUiValue(68), scaleUiValue(24));
    ui->channel_label_edit->setGeometry(scaleUiValue(82), scaleUiValue(78), scaleUiValue(120), scaleUiValue(24));
    ui->channel_button->setGeometry(scaleUiValue(208), scaleUiValue(78), scaleUiValue(58), scaleUiValue(24));

    ui->label_7->setGeometry(scaleUiValue(272), scaleUiValue(14), scaleUiValue(86), scaleUiValue(24));
    ui->lineEdit->setGeometry(scaleUiValue(358), scaleUiValue(14), scaleUiValue(68), scaleUiValue(24));
    ui->label_9->setGeometry(scaleUiValue(430), scaleUiValue(14), scaleUiValue(66), scaleUiValue(24));
    ui->begin_button->setGeometry(scaleUiValue(500), scaleUiValue(14), scaleUiValue(58), scaleUiValue(24));

    ui->label_8->setGeometry(scaleUiValue(272), scaleUiValue(46), scaleUiValue(86), scaleUiValue(24));
    ui->lineEdit_2->setGeometry(scaleUiValue(358), scaleUiValue(46), scaleUiValue(68), scaleUiValue(24));
    ui->label_10->setGeometry(scaleUiValue(430), scaleUiValue(46), scaleUiValue(66), scaleUiValue(24));
    ui->end_button->setGeometry(scaleUiValue(500), scaleUiValue(46), scaleUiValue(58), scaleUiValue(24));

    ui->channel_label_2->setGeometry(scaleUiValue(272), scaleUiValue(78), scaleUiValue(86), scaleUiValue(24));
    ui->lineEdit_3->setGeometry(scaleUiValue(358), scaleUiValue(78), scaleUiValue(52), scaleUiValue(24));
    ui->differential_distance->setGeometry(scaleUiValue(416), scaleUiValue(78), scaleUiValue(58), scaleUiValue(24));
    ui->filter->setGeometry(scaleUiValue(478), scaleUiValue(80), scaleUiValue(86), scaleUiValue(24));
    if (m_highPassLabel) {
        m_highPassLabel->setGeometry(scaleUiValue(272), scaleUiValue(112), scaleUiValue(86), scaleUiValue(24));
    }
    if (m_highPassCutoffEdit) {
        m_highPassCutoffEdit->setGeometry(scaleUiValue(358), scaleUiValue(112), scaleUiValue(52), scaleUiValue(24));
    }
    if (m_highPassConfirmButton) {
        m_highPassConfirmButton->setGeometry(scaleUiValue(416), scaleUiValue(112), scaleUiValue(68), scaleUiValue(24));
    }
    if (m_diffPhaseCheckBox) {
        m_diffPhaseCheckBox->setGeometry(scaleUiValue(490), scaleUiValue(112), scaleUiValue(84), scaleUiValue(24));
    }

    ui->save_all_button->setGeometry(scaleUiValue(14), scaleUiValue(112), scaleUiValue(80), scaleUiValue(26));
    ui->end_save_all_button->setGeometry(scaleUiValue(100), scaleUiValue(112), scaleUiValue(104), scaleUiValue(26));
    ui->save_single_button->setGeometry(scaleUiValue(14), scaleUiValue(144), scaleUiValue(80), scaleUiValue(26));
    ui->end_save_button->setGeometry(scaleUiValue(100), scaleUiValue(144), scaleUiValue(104), scaleUiValue(26));
    if (m_saveConfigButton) {
        m_saveConfigButton->setGeometry(scaleUiValue(14), scaleUiValue(176), scaleUiValue(80), scaleUiValue(26));
    }
    if (m_loadConfigButton) {
        m_loadConfigButton->setGeometry(scaleUiValue(100), scaleUiValue(176), scaleUiValue(104), scaleUiValue(26));
    }

    ui->label->setGeometry(scaleUiValue(218), scaleUiValue(144), scaleUiValue(58), scaleUiValue(18));
    ui->timelabel1->setGeometry(scaleUiValue(276), scaleUiValue(144), scaleUiValue(78), scaleUiValue(18));
    ui->label_2->setGeometry(scaleUiValue(362), scaleUiValue(144), scaleUiValue(58), scaleUiValue(18));
    ui->timelabel2->setGeometry(scaleUiValue(420), scaleUiValue(144), scaleUiValue(78), scaleUiValue(18));
    ui->label_3->setGeometry(scaleUiValue(506), scaleUiValue(144), scaleUiValue(58), scaleUiValue(18));
    ui->timelabel3->setGeometry(scaleUiValue(564), scaleUiValue(144), scaleUiValue(78), scaleUiValue(18));

    ui->label_4->setGeometry(scaleUiValue(218), scaleUiValue(172), scaleUiValue(58), scaleUiValue(18));
    ui->timelabel4->setGeometry(scaleUiValue(276), scaleUiValue(172), scaleUiValue(78), scaleUiValue(18));
    ui->label_5->setGeometry(scaleUiValue(362), scaleUiValue(172), scaleUiValue(58), scaleUiValue(18));
    ui->timelabel5->setGeometry(scaleUiValue(420), scaleUiValue(172), scaleUiValue(78), scaleUiValue(18));
    ui->label_6->setGeometry(scaleUiValue(506), scaleUiValue(172), scaleUiValue(58), scaleUiValue(18));
    ui->timelabel6->setGeometry(scaleUiValue(564), scaleUiValue(172), scaleUiValue(78), scaleUiValue(18));

    ui->label_12->setGeometry(scaleUiValue(218), scaleUiValue(204), scaleUiValue(110), scaleUiValue(18));
    ui->label_14->setGeometry(scaleUiValue(330), scaleUiValue(204), scaleUiValue(156), scaleUiValue(18));
    ui->label_12->setText(QStringLiteral("磁盘剩余空间："));
    ui->label_14->setText(QStringLiteral("-- GB"));

    ui->label_11->setGeometry(scaleUiValue(218), scaleUiValue(232), scaleUiValue(78), scaleUiValue(18));
    ui->label_13->setGeometry(scaleUiValue(300), scaleUiValue(232), scaleUiValue(86), scaleUiValue(18));
    ui->label_11->setText(QStringLiteral("存储耗时："));

    if (!m_saveAllStateDot) {
        m_saveAllStateDot = new QLabel(ui->widget_5);
        m_saveAllStateDot->setObjectName("saveAllStateDot");
    }
    if (!m_saveAllStateLabel) {
        m_saveAllStateLabel = new QLabel(ui->widget_5);
        m_saveAllStateLabel->setObjectName("saveAllStateLabel");
        m_saveAllStateLabel->setText(QStringLiteral("未保存"));
    }
    m_saveAllStateDot->setGeometry(scaleUiValue(500), scaleUiValue(207), scaleUiValue(12), scaleUiValue(12));
    m_saveAllStateLabel->setGeometry(scaleUiValue(518), scaleUiValue(204), scaleUiValue(92), scaleUiValue(18));

    // 抽取系数改为固定下拉选项
    ui->extract_edit->clear();
    ui->extract_edit->addItem("不抽取数据", 1);
    ui->extract_edit->addItem("抽取2倍", 2);
    ui->extract_edit->addItem("抽取4倍", 4);
    ui->extract_edit->addItem("抽取8倍", 8);
    ui->extract_edit->addItem("抽取16倍", 16);
    const int idx8 = ui->extract_edit->findData(8);
    ui->extract_edit->setCurrentIndex(idx8 >= 0 ? idx8 : 0);

    // 光纤长度与脉冲频率一一对应，避免文本误判导致错档。
    ui->length_selection_comboBox->clear();
    ui->length_selection_comboBox->addItem("5km(20Khz)", 20000);
    ui->length_selection_comboBox->addItem("10km(10Khz)", 10000);
    ui->length_selection_comboBox->addItem("30km(3.333Khz)", 3333);
    ui->length_selection_comboBox->addItem("50km(2Khz)", 2000);
    const int idx10km = ui->length_selection_comboBox->findData(10000);
    ui->length_selection_comboBox->setCurrentIndex(idx10km >= 0 ? idx10km : 0);
    updateMonitorScaleLabels();
    updateRmsXAxisModeButtonText();
    updateRmsModeButtonText();
    updateRmsPauseButtonText();
    updateSaveAllUiState();

    applyCardShadow(ui->widget_5, 20, 3);
}

/**
 * @brief 配置本功能所需的控件、图表、连接关系或缓存状态。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::setupPlotToolButtons()
{
    if (ui && ui->centralWidget && ui->verticalLayout && !m_fftToolbar) {
        m_fftToolbar = new QWidget(ui->centralWidget);
        m_fftToolbar->setObjectName("fftToolbar");
        m_fftToolbar->setFixedHeight(scaleUiValue(36));
        m_fftToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *toolbarLayout = new QHBoxLayout(m_fftToolbar);
        toolbarLayout->setContentsMargins(0, 2, 4, 2);
        toolbarLayout->setSpacing(0);
        toolbarLayout->addStretch(1);

        m_fftModeButton = new QPushButton("切换频谱图", m_fftToolbar);
        m_fftModeButton->setObjectName("fftModeToggleBtn");
        m_fftModeButton->setFixedSize(scaleUiValue(128), scaleUiValue(30));
        m_fftModeButton->setFocusPolicy(Qt::NoFocus);
        toolbarLayout->addWidget(m_fftModeButton, 0, Qt::AlignRight | Qt::AlignVCenter);
        ui->verticalLayout->insertWidget(0, m_fftToolbar);

        connect(m_fftModeButton, &QPushButton::clicked, this, &MainWindow::onFftModeButtonClicked);
    }
    if (ui && ui->widget_2 && !m_rmsResetButton) {
        m_rmsResetButton = new QPushButton("初始化视角", ui->widget_2);
        m_rmsResetButton->setObjectName("rmsResetBtn");
        connect(m_rmsResetButton, &QPushButton::clicked, this, &MainWindow::onResetRmsViewClicked);
    }
    if (ui && ui->widget_2 && !m_rmsXAxisModeButton) {
        m_rmsXAxisModeButton = new QPushButton(ui->widget_2);
        m_rmsXAxisModeButton->setObjectName("rmsAxisModeBtn");
        connect(m_rmsXAxisModeButton, &QPushButton::clicked, this, &MainWindow::onToggleRmsXAxisModeClicked);
    }
    if (ui && ui->widget_2 && !m_rmsModeButton) {
        m_rmsModeButton = new QPushButton(ui->widget_2);
        m_rmsModeButton->setObjectName("rmsModeToggleBtn");
        connect(m_rmsModeButton, &QPushButton::clicked, this, &MainWindow::onToggleRmsDisplayModeClicked);
    }
    if (ui && ui->widget_2 && !m_rmsPauseButton) {
        m_rmsPauseButton = new QPushButton(ui->widget_2);
        m_rmsPauseButton->setObjectName("rmsPauseBtn");
        connect(m_rmsPauseButton, &QPushButton::clicked, this, &MainWindow::onRmsPauseButtonClicked);
    }
    if (m_fftModeButton) {
        m_fftModeButton->setText(m_fftLineMode ? "切换瀑布图" : "切换频谱图");
        m_fftModeButton->show();
        m_fftModeButton->raise();
    }
    updateRmsXAxisModeButtonText();
    updateRmsModeButtonText();
    updateRmsPauseButtonText();
}

/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::updateOverlayButtonsGeometry()
{
    const int pad = scaleUiValue(10);
    if (ui && ui->widget_2) {
        const int h = scaleUiValue(26);
        int right = ui->widget_2->width() - pad;
        if (m_rmsXAxisModeButton) {
            const int wMode = scaleUiValue(84);
            right -= wMode;
            m_rmsXAxisModeButton->setGeometry(right, pad, wMode, h);
            m_rmsXAxisModeButton->raise();
            right -= scaleUiValue(6);
        }
        if (m_rmsPauseButton) {
            const int wPause = scaleUiValue(84);
            right -= wPause;
            m_rmsPauseButton->setGeometry(right, pad, wPause, h);
            m_rmsPauseButton->raise();
            right -= scaleUiValue(6);
        }
        if (m_rmsModeButton) {
            const int wToggle = scaleUiValue(92);
            right -= wToggle;
            m_rmsModeButton->setGeometry(right, pad, wToggle, h);
            m_rmsModeButton->raise();
            right -= scaleUiValue(6);
        }
        if (m_rmsResetButton) {
            const int wReset = scaleUiValue(102);
            right -= wReset;
            m_rmsResetButton->setGeometry(right, pad, wReset, h);
            m_rmsResetButton->raise();
        }
    }
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateOverlayButtonsGeometry();
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::syncRmsPlotMode()
{
    if (!m_plotModule || !ui || !ui->widget_2) return;

    const int freq = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int extract = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    const bool strainMode = ui->filter ? (ui->filter->checkState() != Qt::Checked) : false;
    const int displayRows = qMax(1, m_realRows);
    m_frequency = freq;
    m_extractCount = extract;

    if (m_rmsLineMode) {
        m_plotModule->initRMSLinePlot(ui->widget_2,
                                      displayRows,
                                      extract,
                                      freq,
                                      strainMode,
                                      m_rmsXAxisChannelMode);
    } else {
        m_plotModule->initRMSWaterPlot(ui->widget_2, displayRows, extract, freq);
        if (strainMode) {
            m_plotModule->RMStoStrainWaterPlot(ui->widget_2, displayRows, extract, freq);
        } else {
            m_plotModule->StrainToRMSWaterPlot(ui->widget_2, displayRows, extract, freq);
        }
        if (ui->widget_2->plottableCount() > 0) {
            QCPColorMap *map = qobject_cast<QCPColorMap*>(ui->widget_2->plottable(0));
            if (map && map->data()) {
                const double maxX = m_rmsXAxisChannelMode
                                        ? static_cast<double>(displayRows)
                                        : static_cast<double>(MainWindowUtils::calculateMaxX(displayRows, extract, freq));
                ui->widget_2->xAxis->setLabel(m_rmsXAxisChannelMode ? QStringLiteral("通道")
                                                                    : QStringLiteral("位置(m)"));
                ui->widget_2->xAxis->setRange(0, qMax(1.0, maxX));
                map->data()->setRange(QCPRange(0, qMax(1.0, maxX)), map->data()->valueRange());
            }
        }
    }

    if (m_dataModule) {
        m_dataModule->setRMSWaterPlot(ui->widget_2);
        m_dataModule->setFrequency(freq);
        m_dataModule->setRows(displayRows);
        m_dataModule->setExtractCount(extract);
        m_dataModule->setStrainMode(strainMode);
        m_dataModule->setRmsLineMode(m_rmsLineMode);
        m_dataModule->setRmsPaused(m_rmsPaused);
        m_dataModule->setRmsXAxisChannelMode(m_rmsXAxisChannelMode);
        m_dataModule->refreshRmsPlotFromCache();
    }

    updateRmsModeButtonText();
    updateRmsPauseButtonText();
    updateRmsXAxisModeButtonText();
    updateOverlayButtonsGeometry();
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::syncFftPlotMode(int frequency)
{
    if (!m_plotModule || !ui || !ui->widget_3) return;
    if (m_fftLineMode) {
        m_plotModule->initFFTSpectrumPlot(ui->widget_3, frequency);
    } else {
        m_plotModule->initFFTWaterPlot(ui->widget_3, frequency);
    }
    if (m_dataModule) {
        m_dataModule->setFFTWaterPlot(ui->widget_3);
        m_dataModule->setFftLineMode(m_fftLineMode);
    }
    updateOverlayButtonsGeometry();
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onFftModeButtonClicked()
{
    m_fftLineMode = !m_fftLineMode;
    const int freq = ParameterManager::instance().getParameter("pulse_frequency").toInt();
    syncFftPlotMode(freq > 0 ? freq : 10000);
    if (m_fftModeButton) {
        m_fftModeButton->setText(m_fftLineMode ? "切换瀑布图" : "切换频谱图");
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onResetRmsViewClicked()
{
    if (!ui || !ui->widget_2) return;
    const int freq = qMax(1, ParameterManager::instance().getParameter("pulse_frequency").toInt());
    const int extract = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    const bool strainMode = ui->filter ? (ui->filter->checkState() != Qt::Checked) : false;
    const double maxX = m_rmsXAxisChannelMode
                            ? static_cast<double>(qMax(1, m_realRows))
                            : static_cast<double>(MainWindowUtils::calculateMaxX(m_realRows, extract, freq));

    ui->widget_2->xAxis->setLabel(m_rmsXAxisChannelMode ? QStringLiteral("通道") : QStringLiteral("位置(m)"));
    ui->widget_2->xAxis->setRange(0, qMax(1.0, maxX));

    bool applied = false;
    if (m_rmsLineMode) {
        if (ui->widget_2->graphCount() > 0 && ui->widget_2->graph(0) && ui->widget_2->graph(0)->dataCount() > 0) {
            auto graphData = ui->widget_2->graph(0)->data();
            if (graphData && !graphData->isEmpty()) {
                double minVal = 0.0;
                double maxVal = 0.0;
                bool first = true;
                for (auto it = graphData->constBegin(); it != graphData->constEnd(); ++it) {
                    const double value = it->value;
                    if (first) {
                        minVal = maxVal = value;
                        first = false;
                    } else {
                        minVal = qMin(minVal, value);
                        maxVal = qMax(maxVal, value);
                    }
                }
                if (!first) {
                    if (qFuzzyCompare(minVal, maxVal)) {
                        const double span = strainMode ? qMax(1.0, qAbs(minVal) * 0.2) : qMax(0.2, qAbs(minVal) * 0.2);
                        minVal -= span;
                        maxVal += span;
                    } else {
                        const double pad = qMax(strainMode ? 0.8 : 0.15, (maxVal - minVal) * 0.18);
                        minVal -= pad;
                        maxVal += pad;
                    }
                    ui->widget_2->yAxis->setRange(minVal, maxVal);
                    applied = true;
                }
            }
        }
    } else if (ui->widget_2->plottableCount() > 0) {
        QCPColorMap *map = qobject_cast<QCPColorMap*>(ui->widget_2->plottable(0));
        if (map && map->data()) {
            ui->widget_2->xAxis->setRange(map->data()->keyRange());
            ui->widget_2->yAxis->setRange(map->data()->valueRange());
            applied = true;
        }
    }

    if (!applied) {
        ui->widget_2->yAxis->setRange(strainMode ? QCPRange(-50, 50) : QCPRange(0, 6));
        if (!m_rmsLineMode) {
            ui->widget_2->yAxis->setRange(0, 100);
        }
    }
    ui->widget_2->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onToggleRmsXAxisModeClicked()
{
    m_rmsXAxisChannelMode = !m_rmsXAxisChannelMode;
    updateRmsXAxisModeButtonText();

    if (m_dataModule) {
        m_dataModule->setRmsXAxisChannelMode(m_rmsXAxisChannelMode);
    }
    if (!ui || !ui->widget_2) return;

    if (m_rmsLineMode) {
        syncRmsPlotMode();
        return;
    }

    const int freq = ParameterManager::instance().getParameter("pulse_frequency").toInt();
    const int extract = ParameterManager::instance().getParameter("extract_count").toInt();

    bool applied = false;
    if (ui->widget_2->plottableCount() > 0) {
        QCPColorMap *map = qobject_cast<QCPColorMap*>(ui->widget_2->plottable(0));
        if (map && map->data()) {
            const int rows = qMax(1, map->data()->keySize());
            const double maxX = m_rmsXAxisChannelMode
                                    ? static_cast<double>(rows)
                                    : static_cast<double>(MainWindowUtils::calculateMaxX(rows, extract, freq));
            ui->widget_2->xAxis->setLabel(m_rmsXAxisChannelMode ? QStringLiteral("通道") : QStringLiteral("位置(m)"));
            ui->widget_2->xAxis->setRange(0, maxX);
            map->data()->setRange(QCPRange(0, maxX), map->data()->valueRange());
            applied = true;
        }
    }

    if (!applied) {
        const double maxX = m_rmsXAxisChannelMode
                                ? static_cast<double>(qMax(1, m_realRows))
                                : static_cast<double>(MainWindowUtils::calculateMaxX(m_realRows, extract, freq));
        ui->widget_2->xAxis->setLabel(m_rmsXAxisChannelMode ? QStringLiteral("通道") : QStringLiteral("位置(m)"));
        ui->widget_2->xAxis->setRange(0, maxX);
    }

    ui->widget_2->replot(QCustomPlot::rpQueuedReplot);
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onToggleRmsDisplayModeClicked()
{
    m_rmsLineMode = !m_rmsLineMode;
    syncRmsPlotMode();

    if (ui && ui->statusBar) {
        ui->statusBar->showMessage(m_rmsLineMode ? QStringLiteral("RMS图已切换到单帧XY模式")
                                                 : QStringLiteral("RMS图已切换到瀑布模式"),
                                   2000);
    }
}

/**
 * @brief 接收界面事件、参数变化或采集数据，并按当前运行状态协调后续更新。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::onRmsPauseButtonClicked()
{
    m_rmsPaused = !m_rmsPaused;
    updateRmsPauseButtonText();

    if (m_dataModule) {
        m_dataModule->setRmsPaused(m_rmsPaused);
        if (!m_rmsPaused) {
            m_dataModule->refreshRmsPlotFromCache();
        }
    }

    if (ui && ui->statusBar) {
        ui->statusBar->showMessage(m_rmsPaused ? QStringLiteral("RMS图已暂停刷新")
                                               : QStringLiteral("RMS图已恢复刷新"),
                                   2000);
    }
}

/**
 * @brief 清空或复位当前功能相关的状态，确保后续流程从一致状态继续。
 * @details 此实现属于主窗口协调层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void MainWindow::resetAudioPlotView(int panelIndex)
{
    if (panelIndex < 0 || panelIndex >= m_audioPlots.size()) return;
    QCustomPlot *plot = m_audioPlots[panelIndex];
    if (!plot || plot->graphCount() < 2 || !plot->graph(1)) return;

    if (plot->graph(1)->dataCount() > 0) {
        auto data = plot->graph(1)->data();
        if (data && !data->isEmpty()) {
            auto itBegin = data->constBegin();
            auto itEnd = data->constEnd();
            --itEnd;
            const double lastKey = itEnd->key;
            const double firstKey = itBegin->key;
            const double span = qBound(5.0, lastKey - firstKey, kAudioHistorySeconds);
            plot->xAxis->setRange(lastKey, span, Qt::AlignRight);
            plot->graph(1)->rescaleValueAxis();
            const QCPRange yr = plot->yAxis->range();
            const double pad = qMax(0.05, yr.size() * 0.15);
            plot->yAxis->setRange(yr.lower - pad, yr.upper + pad);
        }
    } else {
        plot->xAxis->setRange(0, 5);
        const bool strainMode = ui && ui->filter ? (ui->filter->checkState() != Qt::Checked) : false;
        plot->yAxis->setRange(strainMode ? QCPRange(-50.0, 50.0) : QCPRange(-0.1, 0.1));
    }

    plot->replot(QCustomPlot::rpQueuedReplot);
}
