#ifndef AUDIO_H
#define AUDIO_H

#include <QObject>
#include <QAudioSink>
#include <QAudioFormat>
#include <QVector>

class AudioRingBufferDevice;

class Audio : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 创建音频输出处理器。
     *
     * 实例初始时不立即占用声卡；首次收到有效采样率的数据时才创建输出端，
     * 因而支持运行期间切换脉冲频率。
     */
    explicit Audio(QObject* parent = nullptr);
    /** @brief 停止声卡输出并释放环形缓冲设备与 QAudioSink。 */
    ~Audio() override;

public slots:
    /**
     * @brief 接收一路浮点相位序列并写入实时音频环形缓冲区。
     * @param xdata 待播放的单声道 float 样本。
     * @param len 有效样本数；会被限制在 xdata 的实际长度内。
     * @param frequency 采样率（Hz）；变化时会安全地重建声卡输出端。
     *
     * 旧数据在缓冲区拥塞时优先丢弃，保证实时监听不会因为积压而产生明显延迟。
     */
    void array_handleSignal(const QVector<float>& xdata, int len, int frequency);

private:
    /**
     * @brief 确保当前音频输出端与指定采样率一致。
     *
     * 当采样率变化时停止旧输出端，按新格式创建 QAudioSink 和线程安全环形缓冲区；
     * 若格式未变化则不做重复创建。
     */
    void ensureSinkForFrequency(int frequency);

    QAudioSink* m_sink = nullptr;
    AudioRingBufferDevice* m_ringDevice = nullptr;
    QAudioFormat m_format;
    int m_currentFrequency = 0;
};

#endif // AUDIO_H
