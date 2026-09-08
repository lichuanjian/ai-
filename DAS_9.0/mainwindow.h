#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QLabel>
#include <QVector>
#include <QStringList>
#include <QResizeEvent>

#include "MainWindowPlot.h"
#include "MainWindowData.h"
#include "MainWindowUtils.h"
#include "ParameterManager.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /** @brief 构建主窗口、基础控件引用、显示模块和参数连接。 */
    explicit MainWindow(QWidget *parent = nullptr);
    /** @brief 停止数据渲染后销毁窗口拥有的界面和显示模块。 */
    ~MainWindow();
    /**
     * @brief 开始关闭序列。
     *
     * 标记窗口为关闭状态并通知显示模块停止刷新，使后台排队信号在退出阶段不再访问 UI。
     */
    void beginShutdown();

public slots:
    /** @brief 显示 PCIe 采集阶段的 mode、耗时和频率。 */
    void onReceivePcieTime1(int mode, qint64 time, int freq);
    /** @brief 显示差分重构阶段耗时。 */
    void onReceiveProcessingTime(qint64 time, int freq);
    /** @brief 显示相位解缠/保存快照阶段耗时。 */
    void onPreserveProcessingTime(qint64 time, int freq);
    /** @brief 显示滤波与音频行提取阶段耗时。 */
    void onFilterProcessingTime(qint64 time, int freq);
    /** @brief 显示 RMS 分组计算阶段耗时。 */
    void onRmsProcessingTime(qint64 time, int freq);
    /** @brief 显示 FFT 阶段耗时。 */
    void onFftProcessingTime(qint64 time, int freq);
    /** @brief 显示二进制保存阶段耗时。 */
    void onSaveProcessingTime(qint64 time, int freq);

    /** @brief 将 RMS 数据和其采集元数据交给数据模块；关闭或离线查看时直接忽略。 */
    void onReceiveRMSData(const QList<QVector<float>>& data, int rows, int cols, int freq, int extract)
    {
        if (m_shuttingDown || !m_dataModule) return;
        if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) return;
        m_realRows = rows;
        m_extractCount = extract;
        m_frequency = freq;
        m_dataModule->onReceiveRMSData(data, rows, cols, freq, extract);
    }
    /** @brief 将未滤波支路的应变/相位数据交给数据模块。 */
    void onReceiveStrainData(const QList<QVector<float>>& data, int rows, int cols, int freq, int extract)
    {
        if (m_shuttingDown || !m_dataModule) return;
        if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) return;
        m_realRows = rows;
        m_extractCount = extract;
        m_frequency = freq;
        m_dataModule->onReceiveStrainData(data, rows, cols, freq, extract);
    }
    /** @brief 将单边 FFT 幅度谱交给数据模块显示。 */
    void onReceiveFFTData(QVector<float> data, int len, int freq)
    {
        if (m_shuttingDown || !m_dataModule) return;
        if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) return;
        m_dataModule->onReceiveFFTData(data, len, freq);
    }
    /** @brief 将两路监测行数据转交给各音频图，并刷新通道距离标签。 */
    void onReceiveMultiAudioData(const QList<QVector<float>>& rowsData,
                                 const QVector<int>& channelRows,
                                 int len,
                                 int freq,
                                 int extractCount);

    /** @brief 代表保存工作线程弹出目录选择框，并通过信号回传选择结果。 */
    void onRequestSelectFolder(const QString& saveType);
    /** @brief 在 GUI 线程显示来自后台模块的消息。 */
    void showMessage(const QString& title, const QString& text, QMessageBox::StandardButtons btns);
    /** @brief 更新界面中的剩余磁盘空间显示。 */
    void onDiskSpaceUpdated(double freeGb);
    /** @brief 同步单通道保存按钮与内部会话状态。 */
    void onSingleSaveStateChanged(bool active);
    /** @brief 同步保存全部按钮、状态点和内部会话状态。 */
    void onSaveAllStateChanged(bool active);

signals:
    /** @brief 请求 Sava_data 启动单通道保存会话。 */
    void save_only();
    /** @brief 请求 Sava_data 启动整帧保存会话。 */
    void save_all();
    /** @brief 请求结束当前单通道保存会话。 */
    void end_save();
    /** @brief 请求结束当前整帧保存会话。 */
    void end_save_all();
    /** @brief 将 GUI 线程选择到的目录异步回传给保存工作线程。 */
    void folderSelectedToThread(const QString& saveType, const QString& dir);
    /** @brief 广播滤波开关给解缠与滤波模块，使二者同步重置历史状态。 */
    void is_filter(bool ok);

private slots:
    /** @brief 使采集定时器开始工作。 */
    void onStartButtonClicked();
    /** @brief 请求停止实时采集。 */
    void onStopButtonClicked();
    /** @brief 处理旧滤波复选框状态并同步参数中心。 */
    void on_filter_stateChanged(int state);

    /** @brief 校验并提交监测通道设置。 */
    void onChannelButtonClicked();
    /** @brief 校验并提交空间抽取率设置。 */
    void onExtractButtonClicked();
    /** @brief 校验并提交脉冲频率设置，触发采集端重新配置。 */
    void onPulseFrequencyButtonClicked();
    /** @brief 校验并提交差分距离。 */
    void onDiffDistanceButtonClicked();
    /** @brief 开关相位差分显示/处理模式。 */
    void onDiffPhaseStateChanged(int state);
    /** @brief 校验并提交单通道保存的起始通道。 */
    void onStartSaveButtonClicked();
    /** @brief 校验并提交单通道保存的结束通道。 */
    void onEndSaveButtonClicked();
    /** @brief 发起单通道保存会话。 */
    void onSaveOnlyButtonClicked();
    /** @brief 发起或结束保存全部会话。 */
    void onSaveAllButtonClicked();
    /** @brief 处理当前滤波复选框状态并发射 is_filter。 */
    void onFilterStateChanged(int state);
    /** @brief 处理光纤长度选择，更新可用空间范围。 */
    void onLengthComboIndexChanged(int index);
    /** @brief 校验并提交高通截止频率。 */
    void onHighPassConfirmButtonClicked();
    /** @brief 导出当前运行参数至文本配置文件。 */
    void onSaveConfigClicked();
    /** @brief 导入文本配置并同步全部界面控件。 */
    void onLoadConfigClicked();
    /** @brief 切换 FFT 的曲线/瀑布显示模式。 */
    void onFftModeButtonClicked();
    /** @brief 重置 RMS 图的用户缩放范围。 */
    void onResetRmsViewClicked();
    /** @brief 切换 RMS 曲线的通道/距离横轴。 */
    void onToggleRmsXAxisModeClicked();
    /** @brief 切换 RMS/应变的曲线/瀑布显示模式。 */
    void onToggleRmsDisplayModeClicked();
    /** @brief 暂停或恢复 RMS/应变图实时刷新。 */
    void onRmsPauseButtonClicked();
    /** @brief 选择单点保存目录并打开离线浏览器。 */
    void onViewSavedSingleDataClicked();
    /** @brief 选择保存全部目录或 .bin 并打开离线浏览器。 */
    void onViewSavedAllDataClicked();

private:
    Ui::MainWindow *ui;

    /** @brief 应用全局深色/浅色界面风格、间距和控件样式。 */
    void applyUiTheme();
    /** @brief 给指定卡片控件添加 DPI 适配的投影。 */
    void applyCardShadow(QWidget *target, int blurRadius = 16, int yOffset = 2);
    /** @brief 动态创建两套音频图面板及其通道、保存和复位控件。 */
    void setupAudioPanels();
    /** @brief 从某个音频面板读取并应用监测通道选择。 */
    void applyAudioChannelSelection(int panelIndex);
    /** @brief 以当前频率和抽取率将面板通道换算为距离标签。 */
    void updateAudioDistanceLabel(int panelIndex, int channelIndex, int frequency, int extractCount);
    /** @brief 在窗口缩放后重算叠加在图表上的工具按钮几何位置。 */
    void updateOverlayButtonsGeometry();
    /** @brief 清除指定音频图的缩放与时间线，恢复默认观察窗口。 */
    void resetAudioPlotView(int panelIndex);
    /** @brief 创建 FFT/RMS 图表右上角的模式、复位和暂停工具按钮。 */
    void setupPlotToolButtons();
    /** @brief 配置并通知数据模块使用当前 FFT 显示模式。 */
    void syncFftPlotMode(int frequency);
    /** @brief 按单通道保存状态刷新按钮文字、可用性和提示。 */
    void updateSingleSaveButtons();
    /** @brief 更新监测通道可选范围和面板距离标签。 */
    void updateMonitorScaleLabels();
    /** @brief 按频率和抽取率约束监测通道输入范围。 */
    void syncMonitorChannelRange(bool resetStart);
    /** @brief 配置并通知数据模块使用当前 RMS 显示模式。 */
    void syncRmsPlotMode();
    /** @brief 根据 RMS 横轴模式更新工具按钮文案。 */
    void updateRmsXAxisModeButtonText();
    /** @brief 根据 RMS 曲线/瀑布模式更新工具按钮文案。 */
    void updateRmsModeButtonText();
    /** @brief 根据 RMS 暂停状态更新工具按钮文案。 */
    void updateRmsPauseButtonText();
    /** @brief 更新保存全部的状态点、标签和按钮启用状态。 */
    void updateSaveAllUiState();
    /** @brief 打开 SavedDataViewer，并按模式选择单点或全量数据加载流程。 */
    void openSavedDataViewer(bool loadAllData);
    /** @brief 从参数中心读取最新配置并回写所有相关界面控件。 */
    void syncConfigUiFromParameters();
    /** @brief 将可持久化参数写入 UTF-8 键值文本；失败原因返回 errorMessage。 */
    bool saveConfigToTextFile(const QString& filePath, QString *errorMessage = nullptr) const;
    /** @brief 读取、校验并批量应用 UTF-8 键值文本配置。 */
    bool loadConfigFromTextFile(const QString& filePath, QString *errorMessage = nullptr);
    /** @brief 根据主屏逻辑 DPI 计算统一界面缩放因子。 */
    double computeUiScale() const;
    /** @brief 将设计尺寸以当前界面缩放因子转换为像素整数。 */
    int scaleUiValue(int value) const;
protected:
    /** @brief 窗口尺寸变化后重排覆盖式图表工具按钮。 */
    void resizeEvent(QResizeEvent *event) override;
private:

    MainWindowPlot *m_plotModule = nullptr;
    MainWindowData *m_dataModule = nullptr;

    QComboBox *m_lengthCombo = nullptr;
    QLineEdit *m_channelEdit = nullptr;
    QComboBox *m_extractCombo = nullptr;
    QLineEdit *m_diffDistEdit = nullptr;
    QLineEdit *m_startSaveEdit = nullptr;
    QLineEdit *m_endSaveEdit = nullptr;
    QCheckBox *m_filterBox = nullptr;
    QCheckBox *m_diffPhaseCheckBox = nullptr;
    QLabel *m_highPassLabel = nullptr;
    QLineEdit *m_highPassCutoffEdit = nullptr;
    QPushButton *m_highPassConfirmButton = nullptr;
    QPushButton *m_saveConfigButton = nullptr;
    QPushButton *m_loadConfigButton = nullptr;
    QWidget *m_fftToolbar = nullptr;
    QPushButton *m_fftModeButton = nullptr;
    QPushButton *m_rmsResetButton = nullptr;
    QPushButton *m_rmsXAxisModeButton = nullptr;
    QPushButton *m_rmsModeButton = nullptr;
    QPushButton *m_rmsPauseButton = nullptr;
    QPushButton *m_viewSavedSingleButton = nullptr;
    QPushButton *m_viewSavedAllButton = nullptr;

    QVector<QCustomPlot*> m_audioPlots;
    QVector<QLineEdit*> m_audioChannelEdits;
    QVector<QPushButton*> m_audioChannelButtons;
    QVector<QPushButton*> m_audioSaveButtons;
    QVector<QPushButton*> m_audioEndSaveButtons;
    QVector<QPushButton*> m_audioResetButtons;
    QVector<QLabel*> m_audioDistanceLabels;
    QVector<int> m_audioSelectedChannels = {200, 300};
    QVector<bool> m_audioUseBlue = {true, true};
    int m_activeSavePanel = -1;
    int m_pendingSavePanel = -1;
    bool m_singleSaveActive = false;
    bool m_saveAllActive = false;
    bool m_saveAllPending = false;
    bool m_fftLineMode = false;
    bool m_rmsLineMode = false;
    bool m_rmsPaused = false;
    bool m_rmsXAxisChannelMode = false;
    bool m_endChannelUserEdited = false;
    bool m_shuttingDown = false;
    double m_uiScale = 1.0;
    QLabel *m_saveAllStateDot = nullptr;
    QLabel *m_saveAllStateLabel = nullptr;

    bool m_isRmsPlotActive = true;
    int m_realRows = 1024 * 23 / 8;
    int m_extractCount = 8;
    int m_frequency = 10000;

    /** @brief 根据光纤长度界面文本返回匹配的脉冲频率。 */
    int getPulseFreqByLength(const QString& lengthText);
    /** @brief 返回当前频率下原始帧的有效空间行数。 */
    int getRowsByFrequency(int frequency) const;
    /** @brief 按空间范围、抽取率和差分距离估算可输出的通道数。 */
    int getOutputRowCount(int frequency,
                          int extractCount,
                          int startChannel,
                          int endChannel,
                          int differentialDistance) const;
    /** @brief 返回监听通道允许的最大零基索引。 */
    int getMaxMonitorChannel(int frequency, int extractCount) const;
    /** @brief 返回保存起始通道的最大合法索引。 */
    int getMaxSaveStartChannel(int frequency, int extractCount) const;
    /** @brief 返回保存结束通道的最大合法索引。 */
    int getMaxSaveEndChannel(int frequency, int extractCount) const;

    /** @brief 创建图表和数据模块并建立其所属关系。 */
    void initModules();
    /** @brief 连接 UI 控件、参数中心及显示模块的信号槽。 */
    void connectSignalsSlots();
    /** @brief 从 .ui 中获取控件指针，统一后续访问入口。 */
    void initUIWidgetPointers();
};

#endif // MAINWINDOW_H
