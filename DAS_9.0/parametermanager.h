#ifndef PARAMETERMANAGER_H
#define PARAMETERMANAGER_H

#include <QObject>
#include <QVariant>
#include <QMap>
#include <QMutex>

class ParameterManager : public QObject
{
    Q_OBJECT
public:
    /** @brief 获取线程安全的进程级参数中心单例。 */
    static ParameterManager& instance();
    /**
     * @brief 原子更新一个命名运行参数并广播变化通知。
     * @param key 参数名称。
     * @param value 新值；写入完成后发射 parameterChanged，供各工作线程刷新本地缓存。
     */
    void setParameter(const QString& key, const QVariant& value);
    /**
     * @brief 线程安全地读取一个运行参数。
     * @return 参数不存在时返回无效 QVariant，调用方应自行提供默认值。
     */
    QVariant getParameter(const QString& key) const;

signals:
    /**
     * @brief 在参数表写入成功后广播变更。
     *
     * 接收者通常将该值缓存为本线程配置；信号本身不保证监听者已完成应用。
     */
    void parameterChanged(const QString& key, const QVariant& value);

public:
    mutable QMutex m_mutex;

private:
    /** @brief 仅由 instance() 创建，避免系统中出现多个互不一致的参数表。 */
    ParameterManager(QObject *parent = nullptr);
    ParameterManager(const ParameterManager&) = delete;
    ParameterManager& operator=(const ParameterManager&) = delete;
    ~ParameterManager() = default;
    QMap<QString, QVariant> m_parameters;
};

#endif // PARAMETERMANAGER_H
