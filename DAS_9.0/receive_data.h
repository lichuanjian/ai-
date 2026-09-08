#ifndef RECEIVE_DATA_H
#define RECEIVE_DATA_H

#include <QObject>
#include <QTimer>
#include <atomic>
#include <cstddef>

class receive_data : public QObject {
    Q_OBJECT
public:
    /** @brief 创建 PCIe 轮询采集器，分配两个对齐的大帧缓冲并读取当前全局参数。 */
    explicit receive_data(QObject *parent = nullptr);
    /** @brief 停止定时器并释放 mode 18/28 对齐 DMA 缓冲。 */
    ~receive_data();

    /**
     * @brief 请求停止持续采集。
     *
     * 该方法可跨线程调用；会在对象所属线程中停止精确定时器，避免事件循环退出时的竞态。
     */
    void stop();
    /** @brief 兼容接口；缓冲区已按最大帧容量一次性分配，因此当前无需重新分配。 */
    void updateBufferSizes();

signals:
    /**
     * @brief 发送刚从 FPGA 读取的原始 PCIe 帧。
     * @param data 采集端复用的大帧缓冲，布局为 data[(col * rows + row) * 4 + byte]，即 FPGA 列主序。
     * @param mode 当前硬件 ping-pong 缓冲标识（18 或 28）。
     * @param rows、cols 原始矩阵尺寸；其余参数为本帧处理所需的配置快照。
     * @warning 接收者不得长期保存 data 指针，下一帧会复用该缓冲。
     */
    void Raw_data(unsigned char* data,
                  int mode,
                  int rows,
                  int cols,
                  int frequency,
                  int extractCount,
                  int start,
                  int end,
                  int differentialDistance);
    /**
     * @brief 上报一次完整 C2H 帧读取耗时。
     * @param mode 当前硬件 ping-pong 缓冲标识（18 或 28）。
     * @param totalTime 从开始 DMA 读取到确认缓冲的毫秒数。
     * @param frequency 当前采集频率。
     */
    void pcieProcessingTime(int mode, qint64 totalTime, int frequency);

public slots:
    /**
     * @brief 刷新采集端缓存的全局参数及 FPGA 配置。
     *
     * 对频率变化会暂停轮询、写入相应控制寄存器、更新行数/帧大小后恢复定时器；
     * 其他参数仅更新随帧发出的元数据。
     */
    void onParameterChanged(const QString& key, const QVariant& value);
    /**
     * @brief 定时检查 FPGA 帧就绪标志并读取 mode 18 或 mode 28 的一帧数据。
     *
     * 大帧按 8 MiB 分块从 C2H DMA 读取，读完后确认硬件缓冲并发射 Raw_data。
     * 保存数据查看器打开时会立即返回，避免离线读取与实时采集争用资源。
     */
    void PCIE_recevie();

private:
    std::atomic_bool m_stop{false};
    std::atomic_bool m_savedDataViewerBusy{false};
    QTimer* timer_ = nullptr;

    int extractCount = 8;
    int rows = 1024 * 24;
    int cols = 5000;
    int total_size = rows * cols * 4;
    int frequency = 10000;
    int differentialDistance = 8;

    unsigned char* frame_buf_mode18 = nullptr;
    unsigned char* frame_buf_mode28 = nullptr;

    int startInt = 0;
    int endInt = 0;
};

#endif // RECEIVE_DATA_H
