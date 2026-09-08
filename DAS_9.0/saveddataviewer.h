#ifndef SAVEDDATAVIEWER_H
#define SAVEDDATAVIEWER_H

#include <QDialog>
#include <QVector>

#include "qcustomplot.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QThreadPool;
class QWidget;

class SavedDataViewer : public QDialog
{
    Q_OBJECT

public:
    /** @brief 创建离线保存数据浏览对话框、三块图表和后台线程池。 */
    explicit SavedDataViewer(QWidget *parent = nullptr);
    /** @brief 停止后台任务并解除离线加载保护标志。 */
    ~SavedDataViewer() override;

    /** @brief 读取单点保存目录，绘制时域数据、统计量及频谱。 */
    bool loadSinglePointFolder(const QString& folderPath, QString *errorMessage = nullptr);
    /** @brief 读取保存全部目录并自动选择其中一个 .bin 文件。 */
    bool loadSaveAllFolder(const QString& folderPath, QString *errorMessage = nullptr);
    /** @brief 读取保存全部 .bin 文件；以该文件父目录推断采集元数据。 */
    bool loadSaveAllBinFile(const QString& binPath, QString *errorMessage = nullptr);
    /** @brief 使用显式目录和 .bin 路径加载保存全部数据，适用于外部选取文件。 */
    bool loadSaveAllBinFile(const QString& folderPath, const QString& binPath, QString *errorMessage = nullptr);

private:
    QLabel *m_headerLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QCustomPlot *m_primaryPlot = nullptr;
    QCustomPlot *m_secondaryPlot = nullptr;
    QCustomPlot *m_tertiaryPlot = nullptr;
    QWidget *m_saveAllControls = nullptr;
    QSpinBox *m_channelSpin = nullptr;
    QComboBox *m_fftBandCombo = nullptr;
    QPushButton *m_channelButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QThreadPool *m_workerPool = nullptr;

    QString m_saveAllFolderPath;
    QString m_saveAllBinPath;
    int m_saveAllRows = 0;
    int m_saveAllCols = 0;
    int m_saveAllFrequency = 0;
    int m_saveAllStartChannel = 0;
    int m_saveAllEndChannel = 0;
    double m_currentFftMinDb = 0.0;
    double m_currentFftMaxDb = 0.0;
    QVector<double> m_currentFftKeys;
    QVector<double> m_currentFftValues;
    bool m_saveAllLoading = false;
    bool m_offlineLoadActive = false;

    /** @brief 更新对话框标题和说明文本。 */
    void setHeaderTexts(const QString& title, const QString& infoText);
    /** @brief 清空并统一配置一张图的标题、坐标轴及交互样式。 */
    void prepareGraphPlot(QCustomPlot *plot, const QString& title, const QString& xLabel, const QString& yLabel);
    /** @brief 在单点与全量模式之间切换可见图表数量。 */
    void setPlotVisibleCount(int count);
    /** @brief 显示或隐藏保存全部模式所需的通道与频带控件。 */
    void setSaveAllControlsVisible(bool visible);
    /** @brief 把用户选中的物理通道转换为当前文件的零基行索引。 */
    int selectedSaveAllRow() const;
    /** @brief 后台读取 .bin、带通滤波并构建相位瀑布图；完成后回到 GUI 线程渲染。 */
    void startSaveAllBinLoad(const QString& binPath, int channelRow);
    /** @brief 后台分析指定通道的完整波形和 FFT，避免阻塞界面。 */
    void startSaveAllChannelLoad(int channelRow);
    /** @brief 根据频带选择器更新离线频谱图的 X/Y 轴显示范围。 */
    void updateFftBandRange();
    /** @brief 返回当前选择的离线带通滤波下限与上限（Hz）。 */
    QPair<double, double> selectedBandpassRange() const;
    /** @brief 置位全局离线加载标志，使实时采集链暂时让出资源。 */
    void beginOfflineLoadGuard();
    /** @brief 清除离线加载标志，恢复实时采集链。 */
    void endOfflineLoadGuard();

private slots:
    /** @brief 响应“查看通道”按钮，开始当前通道的后台分析。 */
    void onSaveAllChannelRequested();
    /** @brief 响应频带切换，必要时重新加载带通后的瀑布数据。 */
    void onFftBandChanged(int index);
};

#endif // SAVEDDATAVIEWER_H
