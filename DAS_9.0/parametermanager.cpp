/**
 * @file parametermanager.cpp
 * @brief 实现线程安全的运行参数单例与变更广播。
 *
 * 参数表受 QMutex 保护。setParameter 在更新完成后发射 parameterChanged；工作线程通过各自的槽
 * 将有关键缓存在本地，避免在热路径里反复解析 UI 控件。
 */
#include "ParameterManager.h"

//使用静态局部变量实现懒汉式单例，确保全局唯一实例
ParameterManager& ParameterManager::instance()
{
    static ParameterManager instance;// C++11保证静态变量初始化的线程安全
    return instance;
}

/**
 * @brief 执行该实现单元的界面协调或数据处理步骤，保持既有状态机与调用顺序。
 * @details 此实现属于运行参数集中管理层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
ParameterManager::ParameterManager(QObject *parent)
    : QObject(parent)
{
}

//使用互斥锁保证线程安全，更新参数映射表并通知所有监听者
/**
 * @brief 根据最新数据或参数刷新界面状态，同时保留既有的节流与线程边界。
 * @details 此实现属于运行参数集中管理层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
void ParameterManager::setParameter(const QString& key, const QVariant& value)
{
    QMutexLocker locker(&m_mutex);// 自动加锁/解锁，保护共享资源
    m_parameters[key] = value;// 更新参数映射表
    emit parameterChanged(key, value);// 发射参数变化信号
}

//使用互斥锁保证线程安全读取，若参数不存在则返回空QVariant
/**
 * @brief 计算或返回当前功能所需的查询结果、显示属性或决策条件。
 * @details 此实现属于运行参数集中管理层。它遵循既有的对象所有权、信号槽和 UI 线程约束；
 *          注释只说明职责，不改变函数内的数据校验、分支条件或调用次序。
 */
QVariant ParameterManager::getParameter(const QString& key) const
{
    QMutexLocker locker(&m_mutex);// 自动加锁/解锁
    return m_parameters.value(key);// 返回参数值，不存在则返回默认构造的QVariant
}
