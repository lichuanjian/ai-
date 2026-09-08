/**
 * @file audio.cpp
 * @brief 浮点相位监听的实时声卡输出实现。
 *
 * AudioRingBufferDevice 是只读、线程安全的环形 QIODevice：生产者为数据处理线程，消费者为 QAudioSink。
 * 它按 float 样本边界写入/读取，并在积压时丢弃最旧样本，使监听优先保持“当前实时性”而非完整历史。
 * Audio::ensureSinkForFrequency 管理采样率变更，array_handleSignal 负责验证、复位故障声卡和入队。
 */
#include "audio.h"

#include <QByteArray>
#include <QDebug>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
constexpr int kBytesPerAudioSample = static_cast<int>(sizeof(float));
constexpr int kMinSinkBufferBytes = 16 * 1024;
}

class AudioRingBufferDevice final : public QIODevice
{
public:
/**
 * @brief 执行该实现单元中定义的业务步骤，并维持既有数据流与状态约束。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    explicit AudioRingBufferDevice(QObject* parent = nullptr)
        : QIODevice(parent)
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

/**
 * @brief 清理或复位当前模块维护的状态与资源。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    void resetBuffer(int capacityBytes, int targetBufferedBytes, int bytesPerSample)
    {
        QMutexLocker locker(&m_mutex);

        m_bytesPerSample = qMax(1, bytesPerSample);
        capacityBytes = qMax(m_bytesPerSample, capacityBytes - (capacityBytes % m_bytesPerSample));
        targetBufferedBytes = qMax(m_bytesPerSample, targetBufferedBytes - (targetBufferedBytes % m_bytesPerSample));

        m_capacity = capacityBytes;
        m_targetBufferedBytes = qMin(targetBufferedBytes, m_capacity);
        m_buffer.resize(m_capacity);
        m_head = 0;
        m_tail = 0;
        m_size = 0;
    }

/**
 * @brief 将输入写入当前模块维护的数据通道，并遵守现有容量与同步约束。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    void enqueue(const char* data, int len)
    {
        if (!data || len <= 0) {
            return;
        }

        bool shouldNotify = false;
        QMutexLocker locker(&m_mutex);
        if (m_capacity <= 0) {
            return;
        }

        int alignedLen = len - (len % m_bytesPerSample);
        if (alignedLen <= 0) {
            return;
        }

        if (alignedLen >= m_capacity) {
            data += (alignedLen - m_capacity);
            alignedLen = m_capacity;
            alignedLen -= (alignedLen % m_bytesPerSample);
            m_head = 0;
            m_tail = 0;
            m_size = 0;
        }

        const int overflowBytes = qMax(0, m_size + alignedLen - m_capacity);
        if (overflowBytes > 0) {
            dropOldestUnlocked(overflowBytes);
        }

        shouldNotify = (m_size == 0);
        writeUnlocked(data, alignedLen);

        if (m_targetBufferedBytes > 0 && m_size > m_targetBufferedBytes) {
            dropOldestUnlocked(m_size - m_targetBufferedBytes);
        }

        locker.unlock();
        if (shouldNotify) {
            emit readyRead();
        }
    }

/**
 * @brief 读取并返回当前状态、缓存内容或计算结果，不额外改变对外契约。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    bool isSequential() const override
    {
        return true;
    }

/**
 * @brief 读取并返回当前状态、缓存内容或计算结果，不额外改变对外契约。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    bool atEnd() const override
    {
        return false;
    }

/**
 * @brief 读取并返回当前状态、缓存内容或计算结果，不额外改变对外契约。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    qint64 bytesAvailable() const override
    {
        QMutexLocker locker(&m_mutex);
        return static_cast<qint64>(m_size) + QIODevice::bytesAvailable();
    }

protected:
/**
 * @brief 读取并返回当前状态、缓存内容或计算结果，不额外改变对外契约。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    qint64 readData(char* data, qint64 maxlen) override
    {
        if (!data || maxlen <= 0) {
            return 0;
        }

        QMutexLocker locker(&m_mutex);
        const int requested = static_cast<int>(qMin<qint64>(maxlen, std::numeric_limits<int>::max()));
        if (requested <= 0) {
            return 0;
        }

        const int alignedRequest = requested - (requested % m_bytesPerSample);
        const int readable = qMin(alignedRequest, m_size);
        if (readable > 0) {
            readUnlocked(data, readable);
        }

        if (alignedRequest > readable) {
            std::memset(data + readable, 0, static_cast<size_t>(alignedRequest - readable));
        }
        if (requested > alignedRequest) {
            std::memset(data + alignedRequest, 0, static_cast<size_t>(requested - alignedRequest));
        }

        return requested;
    }

/**
 * @brief 将输入写入当前模块维护的数据通道，并遵守现有容量与同步约束。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    qint64 writeData(const char*, qint64) override
    {
        return -1;
    }

private:
/**
 * @brief 清理或复位当前模块维护的状态与资源。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    void dropOldestUnlocked(int bytesToDrop)
    {
        int alignedDrop = bytesToDrop - (bytesToDrop % m_bytesPerSample);
        if (alignedDrop <= 0) {
            return;
        }

        if (alignedDrop >= m_size) {
            m_head = 0;
            m_tail = 0;
            m_size = 0;
            return;
        }

        m_head = (m_head + alignedDrop) % m_capacity;
        m_size -= alignedDrop;
    }

/**
 * @brief 将输入写入当前模块维护的数据通道，并遵守现有容量与同步约束。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    void writeUnlocked(const char* data, int len)
    {
        const int firstPart = qMin(len, m_capacity - m_tail);
        std::memcpy(m_buffer.data() + m_tail, data, static_cast<size_t>(firstPart));

        const int secondPart = len - firstPart;
        if (secondPart > 0) {
            std::memcpy(m_buffer.data(), data + firstPart, static_cast<size_t>(secondPart));
        }

        m_tail = (m_tail + len) % m_capacity;
        m_size += len;
    }

/**
 * @brief 读取并返回当前状态、缓存内容或计算结果，不额外改变对外契约。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
    void readUnlocked(char* data, int len)
    {
        const int firstPart = qMin(len, m_capacity - m_head);
        std::memcpy(data, m_buffer.constData() + m_head, static_cast<size_t>(firstPart));

        const int secondPart = len - firstPart;
        if (secondPart > 0) {
            std::memcpy(data + firstPart, m_buffer.constData(), static_cast<size_t>(secondPart));
        }

        m_head = (m_head + len) % m_capacity;
        m_size -= len;
    }

    mutable QMutex m_mutex;
    QByteArray m_buffer;
    int m_capacity = 0;
    int m_targetBufferedBytes = 0;
    int m_bytesPerSample = kBytesPerAudioSample;
    int m_head = 0;
    int m_tail = 0;
    int m_size = 0;
};

/**
 * @brief 执行该实现单元中定义的业务步骤，并维持既有数据流与状态约束。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
Audio::Audio(QObject* parent)
    : QObject(parent)
{
}

/**
 * @brief 完成对象析构阶段的收尾释放。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
Audio::~Audio()
{
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_ringDevice) {
        delete m_ringDevice;
        m_ringDevice = nullptr;
    }
}

/**
 * @brief 建立本模块运行所需的资源、状态或关联对象。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void Audio::ensureSinkForFrequency(int frequency)
{
    const int safeFrequency = qMax(1, frequency);
    if (m_sink && m_ringDevice && m_currentFrequency == safeFrequency) {
        return;
    }

    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_ringDevice) {
        delete m_ringDevice;
        m_ringDevice = nullptr;
    }

    m_currentFrequency = safeFrequency;
    m_format = QAudioFormat();
    m_format.setSampleRate(safeFrequency);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Float);

    const int bytesPerSecond = safeFrequency * kBytesPerAudioSample;
    const int sinkBufferBytes = qMax(kMinSinkBufferBytes, bytesPerSecond / 4);
    const int ringTargetBytes = qMax(bytesPerSecond, sinkBufferBytes * 2);
    const int ringCapacityBytes = qMax(bytesPerSecond * 2, ringTargetBytes + sinkBufferBytes * 2);

    m_ringDevice = new AudioRingBufferDevice(this);
    m_ringDevice->resetBuffer(ringCapacityBytes, ringTargetBytes, kBytesPerAudioSample);

    m_sink = new QAudioSink(m_format, this);
    m_sink->setBufferSize(sinkBufferBytes);
    m_sink->setVolume(1.0);
    m_sink->start(m_ringDevice);

    connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (!m_sink) return;
        if (state == QAudio::StoppedState && m_sink->error() != QAudio::NoError) {
            qWarning() << "[Audio] sink stopped, error =" << m_sink->error()
                       << "freq =" << m_currentFrequency;
        }
    });
}

/**
 * @brief 响应异步事件或上游数据到达，并将结果交给当前模块的后续处理流程。
 * @details 此实现属于音频输出缓冲层；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void Audio::array_handleSignal(const QVector<float>& xdata, int len, int frequency)
{
    const int safeLen = qMax(0, qMin(len, xdata.size()));
    if (safeLen <= 0) {
        return;
    }

    ensureSinkForFrequency(frequency);
    if (!m_sink || !m_ringDevice) {
        return;
    }

    if (m_sink->state() == QAudio::StoppedState && m_sink->error() != QAudio::NoError) {
        m_sink->reset();
        m_sink->start(m_ringDevice);
    }

    const int bytesToAppend = safeLen * kBytesPerAudioSample;
    const char* srcBytes = reinterpret_cast<const char*>(xdata.constData());
    m_ringDevice->enqueue(srcBytes, bytesToAppend);
}
