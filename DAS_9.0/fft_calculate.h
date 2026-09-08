#ifndef FFT_CALCULATE_H
#define FFT_CALCULATE_H

#include <QObject>
#include <vector>
#include <utility> // for std::pair

class FFT_Calculate : public QObject
{
    Q_OBJECT
public:
    /** @brief 创建 FFT 计算器；实际 FFT 计划按每帧输入长度临时创建。 */
    FFT_Calculate();
    QVector <float> fft_Vector;
private:
    // 存储FFT结果的二维数组
    std::vector<std::vector<std::pair<float, float>>> m_fftResults;  // 实部和虚部

    /**
     * @brief 预留的复数 FFT 结果计算接口。
     * @param inputData 输入时域数据。
     * @param cols 输入长度。
     * @param result 用于接收实部/虚部对；当前实现会清空该容器，未接入主处理链。
     */
    void computeFFT(const float* inputData, int cols, std::vector<std::pair<float, float>>& result);

signals:
    /**
     * @brief 发送计算完成的单边幅度谱。
     * @param fft_Vector 频点幅值，索引 0 对应直流，末端对应 Nyquist。
     * @param length 有效频点数，通常为输入样本数/2+1。
     * @param frequency 原始时域采样率，供接收者构建频率轴。
     */
    void FFT_OK_Signal(QVector <float> fft_Vector,int length,int frequency);
    /** @brief 发送本帧 FFT 的端到端耗时（毫秒）及其采样率上下文。 */
    void fftProcessingTime(qint64 ms,int frequency);

public slots:
    /**
     * @brief 将监听通道的时域数据转换为单边幅度谱。
     *
     * 使用 FFTW 的单精度实数到复数计划，输出 0 到 Nyquist 频率的模值。
     * FFTW 计划创建与销毁受全局互斥锁保护，避免多线程调用规划器造成不安全访问。
     * @param xdata 时域浮点样本。
     * @param cols 希望参与计算的样本数。
     * @param frequency 与数据同行的采样率，用于下游频率轴标定。
     */
    void FFT_handleSignal(const QVector<float>& xdata , int cols,int frequency);
};

#endif // FFT_CALCULATE_H
