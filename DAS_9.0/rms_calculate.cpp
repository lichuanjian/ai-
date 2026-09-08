/**
 * @file rms_calculate.cpp
 * @brief 将每个空间行按时间窗口压缩为 RMS 定位数据。
 *
 * array_handleSignal 遍历输入矩阵的每一行，以 zhenshu（当前 1000 个采样点）为窗口计算 RMS，
 * 形成“时间分组列表，每组包含所有空间行”这一适合瀑布图的数据布局，并将处理耗时回报主窗口。
 */
#include "rms_calculate.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <chrono>
#include <QMutexLocker>
#include <algorithm>
#include <QDateTime>
#include <QMessageBox>
#include "ParameterManager.h"
int zhenshu = 1000;  // 每组的大小 (全局变量，初始值为 100，表示每组包含1000个数据点)

/**
 * @brief 执行本阶段的数据计算与转换，并保持输入输出序列的既有约束。
 * @details 此实现属于RMS 计算线程；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
rms_calculate::rms_calculate(QObject *parent)
    : QObject(parent), executedCount_(0), lineEdit_(nullptr)
{
    // 初始化定时器
    timer_ = new QTimer(this);
   // connect(timer_, &QTimer::timeout, this, &rms_calculate::sendBatchData);
}

/**
 * @brief 完成对象析构时的停止、断连和资源释放收尾。
 * @details 此实现属于RMS 计算线程；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
rms_calculate::~rms_calculate()
{
    delete timer_;  // 释放定时器内存
    // lineEdit_ 由外部管理，不需要手动删除
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于RMS 计算线程；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void rms_calculate::setLineEdit(QLineEdit *lineEdit)
{
    lineEdit_ = lineEdit;
    // 设置 QLineEdit 的初始值
    if (lineEdit_) {
        lineEdit_->setText(QString::number(zhenshu));  // 将 QLineEdit 的初始值设为 zhenshu（100）
    }
}

#include <QMessageBox>  // 确保包含 QMessageBox 头文件

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于RMS 计算线程；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void rms_calculate::array_handleSignal(float* array, int rows, int cols,int frequency ,int extractCount)
{
    if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) {
        return;
    }
    // 初始化变量
    QElapsedTimer timer1;
    timer1.start(); // 启动计时器，用于统计处理耗时
    const int groupSize = zhenshu;  // 每组的大小，使用全局变量 zhenshu
    QList<QVector<float>> data(cols / zhenshu);  // 创建一个二维 QList，每一列是 QList<float>>
    // 计算方差并将每列数据存入 QList
    for (int i = 0; i < rows; ++i) { // 遍历所有行
        for (int j = 0; j < cols; j += groupSize) { // 按groupSize步长遍历列（分组）
            float variance = calculateRMS(&array[i * cols + j], groupSize, zhenshu);
            // 计算当前组的RMS值，参数为：当前组起始地址、组大小、分组粒度
            int colIdx = j / groupSize;  // 计算当前的列索引
            data[colIdx].append(variance*5);  // 将方差结果加入对应列的 QList
        }
    }
    // 将计算好的数据发送给接收端
    emit sendDataToReceiver(data, rows,cols,frequency,extractCount);
    // qDebug() << "方差用时" << timer1.elapsed();  // 注释掉的调试信息
    qint64 fctime = timer1.elapsed();
    emit rmsProcessingTime(fctime,frequency);
}



// 计算所有数据的平方和的方差
/**
 * @brief 执行本阶段的数据计算与转换，并保持输入输出序列的既有约束。
 * @details 此实现属于RMS 计算线程；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
float rms_calculate::calculateVarianceOptimized(const float* data, int size)
{
    int numGroups = size / zhenshu;// 计算完整组数
    if (size % zhenshu != 0) {
        numGroups++;// 若有余数，增加一组处理剩余数据
    }
    float varianceSum = 0; // 方差总和
    for (int groupIndex = 0; groupIndex < numGroups; groupIndex++) {
        int startIndex = groupIndex * zhenshu;// 当前组起始索引
        // 计算当前组结束索引（避免越界）
        int endIndex = std::min(startIndex + (zhenshu - 1), size - 1);
        int groupSize = endIndex - startIndex + 1;// 当前组实际大小
        float sum = 0;// 数据和
        float sumSquared = 0;// 数据平方和
        for (int i = startIndex; i <= endIndex; i++) {
            sum += data[i];
            sumSquared += data[i] * data[i];
        }
        // 计算当前组的平均值
        float mean = sum / groupSize;
        // 计算当前组的方差（E[x²] - (E[x])²）
        float variance = (sumSquared / groupSize - mean * mean);
        varianceSum += variance;// 累加方差
    }
    return varianceSum;// 返回总方差
}

// 计算 RMS
/**
 * @brief 执行本阶段的数据计算与转换，并保持输入输出序列的既有约束。
 * @details 此实现属于RMS 计算线程；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
float rms_calculate::calculateRMS(const float* data, int size, int zhenshu)
{
    int numGroups = size / zhenshu;// 完整组数
    if (size % zhenshu != 0) {
        numGroups++;// 处理剩余数据
    }
    float rmsSumSquared = 0;// 所有数据的平方和
    int totalElements = 0; // 总数据点数

    // 按组计算平方和
    for (int groupIndex = 0; groupIndex < numGroups; groupIndex++) {
        int startIndex = groupIndex * zhenshu;// 组起始索引
        int endIndex = std::min(startIndex + (zhenshu - 1), size - 1);// 组结束索引（防越界）
        int groupSize = endIndex - startIndex + 1;// 组实际大小

        float sumSquared = 0;// 当前组的平方和
        for (int i = startIndex; i <= endIndex; i++) {
            sumSquared += data[i] * data[i];// 累加平方值
        }

        rmsSumSquared += sumSquared;// 累加所有组的平方和
        totalElements += groupSize; // 累加总数据点数
    }

    // 计算所有数据的平方和的平均值
    float meanSquared = rmsSumSquared / totalElements;

    // 对平均值取平方根得到 RMS
    return std::sqrt(meanSquared);
}


