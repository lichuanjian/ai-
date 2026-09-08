#ifndef FFTW_GUARD_H
#define FFTW_GUARD_H

class QMutex;

/**
 * @brief 返回用于保护 FFTW 计划生命周期的进程级互斥锁。
 *
 * 只应在创建或销毁 fftwf_plan 时持有；执行已创建的计划不需要借此锁串行化。
 */
QMutex& fftwPlannerMutex();

#endif // FFTW_GUARD_H
