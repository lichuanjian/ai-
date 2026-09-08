#ifndef UNWRAP_H
#define UNWRAP_H

#include <QObject>
#include <QFile>
#include <QMessageBox>
#include <QTextStream>
#include <QAtomicInt>
#include <QVector>
#include <QByteArray>
#include "ParameterManager.h"

class Unwrap : public QObject
{
    Q_OBJECT
public:
    /** @brief 创建相位解缠器，并订阅监测行和差分相位开关等运行参数。 */
    explicit Unwrap(QObject *parent = nullptr);


    float unwrap_data = 0;

    QVector<float> m_unwrap_row_memory;
    QVector<float> m_diff_phase_row_memory;
    /**
     * @brief 沿时间方向对二维相位矩阵执行带记忆的 2π 解缠。
     *
     * 每一空间行维护上帧末样本，因而能够消除帧边界处的相位跳变；
     * 行数或流配置变化时必须先重置该记忆。
     */
    void array_unwrap_matrix_memory(float* array, int rows, int cols);
    /**
     * @brief 对已解缠矩阵做相邻时间样本的相位差分。
     *
     * 第一帧/配置切换后会建立历史值，不输出跨不连续段的伪差分。
     */
    void applyDiffPhase(float* array, int rows, int cols);

public slots:
    /**
     * @brief 承接重构矩阵，管理解缠状态、可选差分、保存快照及下游转发。
     *
     * 当流尺寸、抽取率、空间范围或差分距离变化时重置记忆；保存开启时复制整帧或
     * 单行至 QByteArray，保证异步保存使用稳定数据。
     */
    void Receive_rebuild_data(float* array, int rows, int cols, int frequency, int extractCount, int start, int end, int differentialDistance);
    /** @brief 同步滤波开关；状态变化时清空解缠历史，避免重新启用后的首帧跳变。 */
    void Receive_is_filter(bool ok);

signals:
    /** @brief 将解缠（及可选时间差分）后的矩阵交给 Filter。 */
    void Unwrap_Data_Signal(float* array, int rows, int cols, int frequency, int extractCount);
    /** @brief 兼容旧保存支路的指针型信号；主保存链使用下方快照信号。 */
    void Unwrap_Data_SaveSignal(float* array, int rows, int cols, int frequency, int extractCount, int start, int end, int differentialDistance);
    /**
     * @brief 向保存线程发送不可变的处理后数据快照。
     * @param snapshot 字节副本，生命周期独立于实时 ping-pong 矩阵。
     * @param fullFrame true 表示整帧；false 表示 snapshotRow 对应的一行。
     * 其余参数完整保留离线回放需要的采集与空间元数据。
     */
    void Unwrap_Data_SaveSnapshotSignal(const QByteArray& snapshot,
                                        bool fullFrame,
                                        int snapshotRow,
                                        int rows,
                                        int cols,
                                        int frequency,
                                        int extractCount,
                                        int start,
                                        int end,
                                        int differentialDistance);
    /** @brief 发送本帧解缠、可选差分和快照构造的耗时。 */
    void preserveProcessingTime(qint64 ms, int frequency);

private slots:
    /** @brief 响应监测行或差分相位开关变更，并在需要时复位相关历史数据。 */
    void onParameterChanged(const QString& key, const QVariant& value);

private:
    /** @brief 清空逐行解缠记忆，使下一帧从新的连续段开始。 */
    void resetUnwrapMemory();
    /** @brief 清空相位差分历史，避免配置切换时把两段数据相减。 */
    void resetDiffPhaseMemory();
    QFile file2;
    bool isFile2Open;
    float* m_array;
    int m_rows;
    int m_cols;
    int m_frequency;
    int m_lastExtractCount = -1;
    int m_lastStart = -1;
    int m_lastEnd = -1;
    int m_lastDifferentialDistance = -1;
    bool m_lastFilterEnabled = true;
    bool m_diffPhaseEnabled = false;
    bool m_hasDiffPhaseHistory = false;
    int m_snapshotRow = 0;
};

#endif // UNWRAP_H
