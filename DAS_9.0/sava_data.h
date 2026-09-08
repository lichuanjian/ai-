#ifndef SAVA_DATA_H
#define SAVA_DATA_H

#include <QObject>
#include <QFile>
#include <QVariant>
#include <QMutex>
#include <QMessageBox>
#include <QDataStream>
#include <QElapsedTimer>
#include <QByteArray>

class Sava_data : public QObject
{
    Q_OBJECT

public:
    /** @brief 创建保存控制器，初始化会话状态、磁盘预留空间与文件锁。 */
    explicit Sava_data(QObject *parent = nullptr);
    /**
     * @brief 检查目标路径所在卷是否可再容纳 requiredSize 字节。
     *
     * 除待写入量外还保留 m_reservedSpace，以降低长时间连续保存写满系统盘的风险。
     */
    bool checkDiskSpace(const QString& filePath, qint64 requiredSize);
    const qint64 m_reservedSpace;

signals:
    /** @brief 兼容旧保存支路的原始矩阵信号；新链路优先使用快照接口。 */
    void Sava_Data_Signal(float* array, int rows, int cols, int frequency, int extractCount);
    /** @brief 请求 GUI 线程显示目录选择对话框，saveType 为 saveSingle 或 saveAll。 */
    void requestSelectFolder(const QString& saveType);
    /** @brief 请求 GUI 线程以指定标题、正文和按钮集合显示消息。 */
    void showMessageSignal(const QString& title, const QString& text, QMessageBox::StandardButtons buttons);
    /** @brief 通知界面存储卷当前可用空间（GB）。 */
    void freeGBtoUI(double Remaining_disk);
    /** @brief 通知界面单通道保存会话是否已激活。 */
    void singleSaveStateChanged(bool active);
    /** @brief 通知界面整帧保存会话是否已激活。 */
    void saveAllStateChanged(bool active);
    /** @brief 发送一次保存快照处理所耗毫秒数。 */
    void saveProcessingTime(qint64 ms, int frequency);

public slots:
    /** @brief 接收全局参数变化；当前保留为保存模块的扩展入口。 */
    void onParameterChanged(const QString& key, const QVariant& value);
    /** @brief 请求“保存全部”会话，实际目录选择由 requestSelectFolder 异步交给主线程。 */
    void onButtonClicked_saveall();
    /** @brief 请求“保存单通道”会话，锁定触发时的监听通道以保证会话内数据一致。 */
    void onButtonClicked_saveonlyone();
    /** @brief 结束保存全部会话，刷新并关闭当前二进制文件。 */
    void onButtonClicked_end_save_all();
    /** @brief 结束单通道保存会话，刷新并关闭当前二进制文件。 */
    void onButtonClicked_end_save();
    /**
     * @brief 兼容旧信号的保存入口。
     *
     * 接收可变 float 矩阵及其采集元数据，并根据当前会话类型写入完整帧或选中行。
     */
    void Preserve_rebuild_data(float* array, int rows, int cols, int frequency, int extractCount, int start, int end, int differentialDistance);
    /**
     * @brief 写入解缠模块创建的不可变数据快照。
     *
     * 在保存线程内执行，避免调用方的复用缓冲被异步文件 I/O 修改；fullFrame 决定写整帧还是单行。
     */
    void Preserve_rebuild_data_snapshot(const QByteArray& snapshot,
                                        bool fullFrame,
                                        int snapshotRow,
                                        int rows,
                                        int cols,
                                        int frequency,
                                        int extractCount,
                                        int start,
                                        int end,
                                        int differentialDistance);
    /** @brief 加锁刷新、销毁 QDataStream 并关闭当前保存文件。 */
    void closeFile();
    /** @brief 通过信号要求 GUI 线程显示消息框，避免工作线程直接操作界面。 */
    void showMessage(const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::NoButton);
    /** @brief 接收 GUI 返回的目录选择结果，并初始化相应保存会话。 */
    void onFolderSelected(const QString& saveType, const QString& dir);

private:
    bool isFile2Open;
    float* m_array;
    int m_rows;
    int m_cols;
    int m_frequency;
    QFile file2;

    QFile m_binFile;
    QDataStream* m_binStream;
    QMutex m_fileMutex;
    bool m_saveSingleFlag;
    bool m_saveAllFlag;
    int m_lockedSingleMonitorPosition = -1;
    int m_pendingSingleMonitorPosition = -1;

    /** @brief 按滚动文件编号在当前会话目录创建下一个 .bin 文件并绑定数据流。 */
    bool createNewBinFile();
    /**
     * @brief 在文件锁保护下创建目录、元数据和第一个数据文件。
     * @return 成功时建立完整会话状态；失败时会保持未激活状态并报告错误。
     */
    bool initializeSaveSessionLocked(const QString& saveType,
                                     int rows,
                                     int cols,
                                     int frequency,
                                     int extractCount,
                                     int start,
                                     int end,
                                     int differentialDistance);

    double Pitch = 0;
    double interval = 0;
    double m_start = 0;
    double m_end = 0;

    qint64 m_currentFileSize = 0;
    int m_fileIndex = 0;
    QString m_pendingRootFolderPath;
    QString m_baseFolderPath;
    QString m_baseFileName;
    qint64 m_cachedAvailableSpaceBytes = -1;
    QString m_cachedStorageRoot;
    QElapsedTimer m_diskCheckTimer;
    QElapsedTimer m_flushTimer;
};

#endif // SAVA_DATA_H
