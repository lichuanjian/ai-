#ifndef REBUILD_DATA_H
#define REBUILD_DATA_H

#include <QObject>
#include <QElapsedTimer>
#include <cstddef>

class MainWindow;

class Rebuild_data : public QObject
{
    Q_OBJECT
public:
    /** @brief 创建原始 PCIe 帧重构器；parent 参数保留以兼容 Qt 对象所有权。 */
    explicit Rebuild_data(MainWindow* parent = nullptr);
    /**
     * @brief 预留的二维数据导出接口。
     *
     * 当前为无副作用占位实现，保留该声明是为兼容历史调用；实际保存由 Sava_data 处理。
     */
    void saveRowsToCSV(float (*array)[5000], int rows, int cols, int arrayIndex);

signals:
    /**
     * @brief 发送已完成空间差分和抽取的行主序 float 矩阵。
     * @param array 输出缓冲首地址；由 mode 专属 ping-pong 缓冲提供。
     * @param rows 输出空间行数。
     * @param cols 每行时间采样数。
     * 其余参数描述生成该矩阵的采集与空间配置。
     */
    void widget_my_array_Signal(float* array,
                                int rows,
                                int cols,
                                int frequency,
                                int extractCount,
                                int start,
                                int end,
                                int differentialDistance);
    /** @brief 发送纯重构阶段耗时（不含下游槽排队/执行时间）。 */
    void totalProcessingTime(qint64 totalTime, int frequency);

public slots:
    /**
     * @brief 将 FPGA 的列主序原始 DMA 帧重构为提取后的差分相位矩阵。
     *
     * 该槽按 mode 18/28 选择独立 ping-pong 输出缓冲；将原始四字节样本解码后，
     * 计算相距 differentialDistance 的相位差，并以 extractCount 进行空间抽取。
     * 完成后把同一缓冲区交给解缠模块，并上报仅含重构阶段的耗时。
     */
    void Receive_raw_data(unsigned char* rawData,
                          int mode,
                          int real_rows,
                          int real_cols,
                          int frequency,
                          int extractCount,
                          int start,
                          int end,
                          int differentialDistance);
};

#endif // REBUILD_DATA_H
