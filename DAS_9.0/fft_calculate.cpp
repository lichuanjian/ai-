/**
 * @file fft_calculate.cpp
 * @brief 单监听通道的实数 FFT 与单边幅度谱计算。
 *
 * 处理函数对有效输入创建 FFTW r2c 单精度计划，执行后把每个复数频点转换为模值；
 * 输出长度为 N/2+1，覆盖直流至 Nyquist。FFTW 的计划创建和销毁由 fftwPlannerMutex 串行保护。
 */
#include "fft_calculate.h"
#include <QDebug>
#include <fftw3.h>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <cmath>  // 纭繚sqrt鍙敤
#include "ParameterManager.h"
#include "fftw_guard.h"

/**
 * @brief 执行本函数负责的计算或数据变换步骤。
 * @details 此实现属于频谱计算线程；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
FFT_Calculate::FFT_Calculate() {}

/**
 * @brief 执行该实现单元中定义的业务步骤，并维持既有数据流与状态约束。
 * @details 此实现属于频谱计算线程；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void FFT_Calculate::computeFFT(const float* inputData, int cols, std::vector<std::pair<float, float>>& result)
{
    // 璇ュ嚱鏁拌嫢闇€鍚敤锛岄渶鍚屾鏀逛负鍗曠簿搴︾増鏈紝姝ゅ鍏堜繚鐣欑┖瀹炵幇
    result.clear();
}

/**
 * @brief 执行本函数负责的计算或数据变换步骤。
 * @details 此实现属于频谱计算线程；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
void FFT_Calculate::FFT_handleSignal(const QVector<float>& xdata, int cols,int frequency)
{
    if (ParameterManager::instance().getParameter("saved_data_viewer_busy").toBool()) {
        return;
    }
    QElapsedTimer timer1;
    timer1.start();
    const int safeCols = qMin(cols, xdata.size());
    if (safeCols <= 0) {
        return;
    }
    fft_Vector.resize(safeCols / 2 + 1);

    // Allocate FFT output buffer with FFTW allocator.
    fftwf_complex *outputData = (fftwf_complex*)fftwf_malloc((safeCols / 2 + 1) * sizeof(fftwf_complex));
    if (!outputData) {
        //qDebug() << "FFTW鍐呭瓨鍒嗛厤澶辫触锛?;
        return;
    }

    fftwf_plan plan = nullptr;
    {
        QMutexLocker plannerLocker(&fftwPlannerMutex());
        plan = fftwf_plan_dft_r2c_1d(
            safeCols,
            const_cast<float*>(xdata.data()),
            outputData,
            FFTW_ESTIMATE
            );
    }
    if (!plan) {
        fftwf_free(outputData);
        return;
    }

    // 鎵цFFT
    fftwf_execute(plan);

    // 璁＄畻骞呭害锛堟棤闇€棰濆magnitudeArray锛岀洿鎺ュ瓨鍒癴ft_Vector锛?
    for (int i = 0; i < safeCols / 2 + 1; ++i) {
        float realPart = outputData[i][0];
        float imagPart = outputData[i][1];
        fft_Vector[i] = std::sqrt(realPart * realPart + imagPart * imagPart);
    }

    // 鍙戦€佷俊鍙?
   emit FFT_OK_Signal(fft_Vector, safeCols / 2 + 1, frequency);

    // ========== 鏍稿績淇2锛氱敤fftwf_free閲婃斁鍐呭瓨锛堟浛浠elete[]锛?==========
    {
        QMutexLocker plannerLocker(&fftwPlannerMutex());
        fftwf_destroy_plan(plan);
    }
    fftwf_free(outputData);  // 鍖归厤fftwf_malloc

    // 鍙戦€佽€楁椂淇″彿
    qint64 elapsedTime = timer1.elapsed();
    emit fftProcessingTime(elapsedTime, frequency);
}


