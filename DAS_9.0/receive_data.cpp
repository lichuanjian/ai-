/**
 * @file receive_data.cpp
 * @brief PCIe C2H 实时帧采集与 FPGA 控制参数同步。
 *
 * 定时器检查控制寄存器 16 的 ping-pong 帧标识；当 mode 18 或 28 就绪时，按 8 MiB 分块读取完整 DMA 帧、
 * 确认硬件缓冲，再将带有频率、抽取率和空间范围元数据的原始缓冲发给 Rebuild_data。
 * 两个 4 KiB 对齐的大容量缓冲在构造期一次性分配，避免高频采集循环中的内存分配抖动。
 */
#include "receive_data.h"

#include <QElapsedTimer>
#include <QDebug>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <cstdint>
#include <malloc.h>

#include "ParameterManager.h"
#include "pcie_fun.h"

namespace {
constexpr int kChunkBytes = 8 * 1024 * 1024;
constexpr int kPollIntervalMs = 1;
constexpr int kMaxRows = 1024 * 24;
constexpr int kMaxCols = 5000;
constexpr int kMaxSamples = kMaxRows * kMaxCols;
constexpr int kMaxBytes = kMaxSamples * static_cast<int>(sizeof(float));
constexpr size_t kFrameBufferBytes = static_cast<size_t>(kMaxBytes);

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
inline int rowsByFrequency(int frequency)
{
    if (frequency == 20000) return 1024 * 11;
    if (frequency == 10000 || frequency == 100000) return 1024 * 23;
    if (frequency == 3333 || frequency == 2000) return 1024 * 24;
    return 1024 * 23;
}

/**
 * @brief 读取、解析或计算本步骤所需的数据结果，并遵守现有的边界检查。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
inline bool readPcieFrame(int baseOffset,
                          int totalSize,
                          unsigned char* frameBuffer)
{
    if (!frameBuffer || totalSize <= 0) {
        return false;
    }

    int transferred = 0;
    while (transferred < totalSize) {
        const int remaining = totalSize - transferred;
        const int transferSize = std::min(remaining, kChunkBytes);
        if (c2h_transfer(baseOffset + transferred,
                         transferSize,
                         frameBuffer + transferred) != 0) {
            qWarning() << "c2h_transfer failed at offset" << (baseOffset + transferred)
                       << "size" << transferSize;
            return false;
        }
        transferred += transferSize;
    }
    return true;
}
} // namespace

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
receive_data::receive_data(QObject *parent)
    : QObject(parent)
{
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &receive_data::PCIE_recevie);
    connect(&ParameterManager::instance(),
            &ParameterManager::parameterChanged,
            this,
            &receive_data::onParameterChanged);

    bool okFreq = false;
    const int configuredFrequency = ParameterManager::instance().getParameter("pulse_frequency").toInt(&okFreq);
    if (okFreq && configuredFrequency > 0) {
        frequency = configuredFrequency;
    }
    rows = rowsByFrequency(frequency);
    cols = 5000;
    total_size = rows * cols * 4;

    extractCount = qMax(1, ParameterManager::instance().getParameter("extract_count").toInt());
    startInt = qMax(0, ParameterManager::instance().getParameter("start_save").toInt());
    endInt = qMax(startInt, ParameterManager::instance().getParameter("end_save").toInt());
    differentialDistance = qMax(1, ParameterManager::instance().getParameter("differential_distance").toInt());
    m_savedDataViewerBusy.store(ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool(),
                                std::memory_order_release);

    frame_buf_mode18 = static_cast<unsigned char*>(_aligned_malloc(kFrameBufferBytes, 4096));
    frame_buf_mode28 = static_cast<unsigned char*>(_aligned_malloc(kFrameBufferBytes, 4096));
    if (!frame_buf_mode18 || !frame_buf_mode28) {
        qCritical() << "receive_data buffer allocation failed";
    }
}

/**
 * @brief 完成对象析构时的停止、断连和资源释放收尾。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
receive_data::~receive_data()
{
    stop();
    if (frame_buf_mode18) {
        _aligned_free(frame_buf_mode18);
        frame_buf_mode18 = nullptr;
    }
    if (frame_buf_mode28) {
        _aligned_free(frame_buf_mode28);
        frame_buf_mode28 = nullptr;
    }
}

/**
 * @brief 清理当前处理阶段的临时状态与持有资源，恢复可预测的后续运行条件。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void receive_data::stop()
{
    m_stop.store(true, std::memory_order_release);
    if (!timer_) {
        return;
    }

    QThread* ownerThread = thread();
    if (!ownerThread || !ownerThread->isRunning()) {
        return;
    }

    if (QThread::currentThread() == ownerThread) {
        if (timer_->isActive()) {
            timer_->stop();
        }
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        if (timer_ && timer_->isActive()) {
            timer_->stop();
        }
    }, Qt::BlockingQueuedConnection);
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void receive_data::updateBufferSizes()
{
    // Buffers are allocated to max capacity once, no-op by design.
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void receive_data::onParameterChanged(const QString& key, const QVariant& value)
{
    if (key == "pulse_frequency") {
        if (timer_) timer_->stop();
        frequency = value.toInt();
        rows = rowsByFrequency(frequency);
        cols = 5000;
        total_size = rows * cols * 4;

        if (frequency == 20000) {
            write_control(0, 0x6C2030d4);
            QThread::sleep(1);
            write_control(0, 0x852061a8);
            QThread::sleep(1);
        } else if (frequency == 10000) {


            write_control(0, 0x6C2061A8);
            QThread::sleep(1);
           write_control(0, 0x852061a8);
           QThread::sleep(1);
            write_control(0, 0x6C400101);
            QThread::sleep(1);
            write_control(0, 0x852061a8);
        } else if (frequency == 3333) {
            write_control(0, 0x6C2124f8);
            QThread::sleep(1);
            write_control(0, 0x852061a8);
            QThread::sleep(1);
            write_control(0, 0x6C400101);
            QThread::sleep(1);
            write_control(0, 0x852061a8);
        } else if (frequency == 2000) {
            write_control(0, 0x6C21e848);
            QThread::sleep(1);
            write_control(0, 0x852061a8);
            QThread::sleep(1);
        }

        if (timer_) timer_->start(kPollIntervalMs);
    } else if (key == "extract_count") {
        extractCount = qMax(1, value.toInt());
    } else if (key == "start_save") {
        startInt = qMax(0, value.toInt());
    } else if (key == "end_save") {
        endInt = qMax(startInt, value.toInt());
    } else if (key == "differential_distance") {
        differentialDistance = qMax(1, value.toInt());
    } else if (key == "saved_data_viewer_busy") {
        m_savedDataViewerBusy.store(value.toBool(), std::memory_order_release);
    }
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于PCIe 数据接收与分帧层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void receive_data::PCIE_recevie()
{
    if (m_stop.load(std::memory_order_acquire)) return;
    if (m_savedDataViewerBusy.load(std::memory_order_acquire)) return;
    if (!frame_buf_mode18 || !frame_buf_mode28) return;
    if (total_size <= 0 || total_size > kMaxBytes) return;

    uint32_t wu1 = 0;
    read_control(16, &wu1);

    if (wu1 == 18) {
        QElapsedTimer timer;
        timer.start();
        if (!readPcieFrame(0, total_size, frame_buf_mode18)) {
            qWarning() << "readPcieFrame mode18 failed";
            return;
        }
        write_control(16, 1);
        emit Raw_data(frame_buf_mode18, 18, rows, cols, frequency, extractCount, startInt, endInt, differentialDistance);
        emit pcieProcessingTime(18, timer.elapsed(), frequency);
    } else if (wu1 == 28) {
        QElapsedTimer timer;
        timer.start();
        if (!readPcieFrame(total_size, total_size, frame_buf_mode28)) {
            qWarning() << "readPcieFrame mode28 failed";
            return;
        }
        write_control(16, 1);
        emit Raw_data(frame_buf_mode28, 28, rows, cols, frequency, extractCount, startInt, endInt, differentialDistance);
        emit pcieProcessingTime(28, timer.elapsed(), frequency);
    }
}
