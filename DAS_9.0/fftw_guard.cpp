/** @file fftw_guard.cpp
 * @brief 提供 FFTW 计划器共享锁的唯一实现，避免多个线程并发修改规划器内部状态。
 */
#include "fftw_guard.h"

#include <QMutex>

QMutex& fftwPlannerMutex()
{
    static QMutex mutex;
    return mutex;
}
