#ifndef RMS_CALCULATE_H
#define RMS_CALCULATE_H

#include <QObject>
#include <QVector>
#include <QTimer>
#include <QMutex>
#include <QLineEdit>

/**
 * @brief RMS计算类：负责计算数据的均方根值(RMS)并批量发送
 */
class rms_calculate : public QObject {
    Q_OBJECT

public:
    /** @brief 创建 RMS 处理器及其预留定时器。 */
    explicit rms_calculate(QObject *parent = nullptr);
    /** @brief 释放内部定时器；外部传入的 QLineEdit 不归本类所有。 */
    ~rms_calculate();

    QVector<float> allVarianceData;  // 存储所有方差数据
    int totalDataCount;               // 数据总量

    int executedCount_ = 0;           // 定时器执行次数
    QTimer *timer_;                   // 定时器
    QMutex mutex_;                    // 互斥锁
    float* array_globalXData = nullptr;  // 全局数组指针

    /**
     * @brief 设置用于显示分组长度的编辑框。
     * @note 编辑框由调用方拥有，本类只保存非拥有指针并写入当前默认分组值。
     */
    void setLineEdit(QLineEdit *lineEdit);

public slots:
    /**
     * @brief 按行、按固定时间窗口计算 RMS，并将结果组织为“时间组 x 空间行”数据。
     *
     * 输入矩阵为行优先布局。每一行按 zhenshu（当前为 1000）个样本分组，
     * 每组计算平方均值的平方根；处理结果及耗时异步发送给主窗口。
     */
    void array_handleSignal(float* array, int rows, int cols, int frequency, int extractCount);

signals:
    /** @brief 兼容旧界面的方差结果信号；当前主窗口不依赖该支路。 */
    void my_array_fangchaSignal(QVector<float> data, int rows, int cols, int extractCount);
    /**
     * @brief 发送按时间窗口分组的 RMS 定位数据。
     * @param data 外层为时间组，内层为各空间行的 RMS 值。
     * @param rows 原始/空间行数，cols 原始时间样本数。
     * @param frequency 采样率，extractCount 空间抽取率。
     */
    void sendDataToReceiver(const QList<QVector<float>>& data, int rows, int cols, int frequency, int extractCount);
    /** @brief 发送本帧 RMS 分组计算耗时（毫秒）。 */
    void rmsProcessingTime(qint64 ms, int frequency);

private:
    /** @brief 分组计算 E[x²] - E[x]² 并返回各组方差之和；当前为辅助/预留计算。 */
    float calculateVarianceOptimized(const float* data, int size);
    /** @brief 对给定样本区间计算均方根值，内部兼容不满 zhenshu 的尾组。 */
    float calculateRMS(const float* data, int size, int zhenshu);

    QLineEdit *lineEdit_;  // 存储QLineEdit指针
};

#endif // RMS_CALCULATE_H
