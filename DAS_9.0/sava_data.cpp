/**
 * @file sava_data.cpp
 * @brief 实时数据的单通道/全帧二进制保存、滚动文件与磁盘保护。
 *
 * Sava_data 通过 GUI 线程异步选择目录，在锁保护下创建带采集元数据的会话目录，并持续写入来自
 * Unwrap 的不可变快照。全量保存写整帧，单点保存写锁定监测行；每次写入检查可用磁盘并按大小滚动 .bin 文件。
 */
#include "sava_data.h"
#include "ParameterManager.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QDir>
#include <QDateTime>
#include <QDataStream>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QMutex>
#include <QApplication>
#include <QFileInfo>
#include <QStorageInfo>
#include <QByteArray>
#include <cstring>
#include <limits>

namespace {
/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
inline int rowsByFrequencyForSave(int frequency)
{
    if (frequency == 20000) return 1024 * 11;
    if (frequency == 10000 || frequency == 100000) return 1024 * 23;
    if (frequency == 3333 || frequency == 2000) return 1024 * 24;
    return 1024 * 23;
}

/**
 * @brief 执行该实现单元定义的数据处理步骤，保持原有的数据所有权和调用顺序。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
inline double meterPerRawPointForFrequency(int frequency)
{
    if (frequency == 3333) return 1.2;
    if (frequency == 2000) return 2.0;
    return 0.4;
}
}

Sava_data::Sava_data(QObject *parent)
    : QObject(parent),
      isFile2Open(false),
      m_array(nullptr),
      m_rows(0),
      m_cols(0),
      m_frequency(0),
      m_binStream(nullptr),
      m_saveSingleFlag(false),
      m_reservedSpace(100 * 1024 * 1024),
      m_saveAllFlag(false)
{
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool Sava_data::checkDiskSpace(const QString& filePath, qint64 requiredSize)
{
    const QFileInfo fileInfo(filePath);
    const QString storageRoot = fileInfo.absolutePath();

    qint64 availableSpaceBytes = -1;
    const bool canUseCached =
        m_diskCheckTimer.isValid() &&
        (m_cachedStorageRoot == storageRoot) &&
        (m_cachedAvailableSpaceBytes >= 0) &&
        (m_diskCheckTimer.elapsed() < 1000);

    if (canUseCached) {
        availableSpaceBytes = m_cachedAvailableSpaceBytes;
    } else {
        QStorageInfo storageInfo(storageRoot);
        storageInfo.refresh();
        if (!storageInfo.isValid() || !storageInfo.isReady()) {
            emit showMessageSignal("错误", "无法获取磁盘信息：" + storageInfo.rootPath(), QMessageBox::Ok);
            return false;
        }
        availableSpaceBytes = storageInfo.bytesAvailable();
        m_cachedAvailableSpaceBytes = availableSpaceBytes;
        m_cachedStorageRoot = storageRoot;
        if (m_diskCheckTimer.isValid()) {
            m_diskCheckTimer.restart();
        } else {
            m_diskCheckTimer.start();
        }
    }

    const bool needUiUpdate = !canUseCached;
    double availableSpaceGB = availableSpaceBytes / (1024.0 * 1024.0 * 1024.0);
    if (needUiUpdate) {
        emit freeGBtoUI(availableSpaceGB);
    }

    qint64 actualRequiredBytes = requiredSize + m_reservedSpace;

    if (availableSpaceBytes < actualRequiredBytes) {
        QString msg = QString("磁盘空间不足！\n"
                              "磁盘：%1\n"
                              "可用空间：%2 GB（%3 MB）\n"
                              "需要空间：%4 MB（含100MB预留）")
                          .arg(storageRoot)
                          .arg(availableSpaceGB, 0, 'f', 2)
                          .arg(availableSpaceBytes / (1024 * 1024))
                          .arg(actualRequiredBytes / (1024 * 1024));
        emit showMessageSignal("错误", msg, QMessageBox::Ok);
        return false;
    }

    return true;
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::onParameterChanged(const QString& key, const QVariant& value)
{
    Q_UNUSED(key);
    Q_UNUSED(value);
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::onButtonClicked_saveall()
{
    QMutexLocker locker(&m_fileMutex);
    if (m_saveAllFlag || m_saveSingleFlag) {
        emit showMessageSignal("提示", "当前已有保存任务在进行，请先结束保存。", QMessageBox::Ok);
        return;
    }
    closeFile();
    emit requestSelectFolder("saveAll");
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::onButtonClicked_saveonlyone()
{
    QMutexLocker locker(&m_fileMutex);
    if (m_saveSingleFlag || m_saveAllFlag) {
        emit showMessageSignal("提示", "当前已有保存任务在进行，请先结束保存。", QMessageBox::Ok);
        if (m_saveSingleFlag) emit singleSaveStateChanged(true);
        return;
    }

    bool ok = false;
    int requestedRow = ParameterManager::instance().getParameter("singleSaveRow").toInt(&ok);
    if (!ok || requestedRow < 0) {
        requestedRow = ParameterManager::instance().getParameter("monitorPosition").toInt(&ok);
    }
    if (!ok || requestedRow < 0) {
        requestedRow = 0;
    }
    m_pendingSingleMonitorPosition = requestedRow;

    closeFile();
    emit requestSelectFolder("saveSingle");
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::onFolderSelected(const QString& saveType, const QString& dir)
{
    if (dir.isEmpty()) {
        emit showMessageSignal("提示", "未选择文件夹，保存已取消", QMessageBox::Ok);
        if (saveType == "saveAll") {
            m_saveAllFlag = false;
            ParameterManager::instance().setParameter("save_all_active", false);
            emit saveAllStateChanged(false);
        }
        if (saveType == "saveSingle") {
            m_lockedSingleMonitorPosition = -1;
            m_pendingSingleMonitorPosition = -1;
            ParameterManager::instance().setParameter("save_single_active", false);
            ParameterManager::instance().setParameter("active_single_save_row", -1);
            emit singleSaveStateChanged(false);
        }
        return;
    }

    QMutexLocker locker(&m_fileMutex);
    closeFile();

    m_fileIndex = 0;
    m_currentFileSize = 0;
    m_pendingRootFolderPath = dir;
    m_baseFolderPath.clear();
    m_baseFileName.clear();

    if (saveType == "saveAll") {
        m_saveAllFlag = true;
        ParameterManager::instance().setParameter("save_all_active", true);
        ParameterManager::instance().setParameter("save_single_active", false);
        emit saveAllStateChanged(true);
        emit showMessageSignal("提示", "保存全部数据已准备，收到第一帧后将按真实 rows/cols 创建文件夹。", QMessageBox::Ok);
    } else if (saveType == "saveSingle") {
        if (m_pendingSingleMonitorPosition >= 0) {
            m_lockedSingleMonitorPosition = m_pendingSingleMonitorPosition;
        } else {
            bool ok = false;
            const int monitorPos = ParameterManager::instance().getParameter("singleSaveRow").toInt(&ok);
            m_lockedSingleMonitorPosition = (ok && monitorPos >= 0) ? monitorPos : 0;
        }
        m_pendingSingleMonitorPosition = -1;
        m_saveSingleFlag = true;
        ParameterManager::instance().setParameter("save_single_active", true);
        ParameterManager::instance().setParameter("save_all_active", false);
        ParameterManager::instance().setParameter("active_single_save_row", m_lockedSingleMonitorPosition);
        emit singleSaveStateChanged(true);
        qDebug() << "[SingleSave][START]"
                 << "ts=" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
                 << "lockedRow=" << m_lockedSingleMonitorPosition;
        emit showMessageSignal("提示", "单点数据保存已准备，收到第一帧后将按真实 rows/cols 创建文件夹。", QMessageBox::Ok);
    }
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::onButtonClicked_end_save_all()
{
    QMutexLocker locker(&m_fileMutex);
    m_saveAllFlag = false;
    ParameterManager::instance().setParameter("save_all_active", false);
    emit saveAllStateChanged(false);
    m_pendingRootFolderPath.clear();
    closeFile();

    showMessage("提示", "全部数据保存已结束");
}

/**
 * @brief 处理上游到达的数据或设备事件，并将其交给既有的数据管线继续传递。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::onButtonClicked_end_save()
{
    QMutexLocker locker(&m_fileMutex);
    m_saveSingleFlag = false;
    m_lockedSingleMonitorPosition = -1;
    m_pendingSingleMonitorPosition = -1;
    ParameterManager::instance().setParameter("save_single_active", false);
    ParameterManager::instance().setParameter("active_single_save_row", -1);
    emit singleSaveStateChanged(false);
    m_pendingRootFolderPath.clear();
    closeFile();
    showMessage("提示", "保存已结束");
}

/**
 * @brief 清理当前处理阶段的临时状态与持有资源，恢复可预测的后续运行条件。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::closeFile()
{
    if (m_binStream) {
        delete m_binStream;
        m_binStream = nullptr;
    }
    if (m_binFile.isOpen()) {
        m_binFile.close();
    }
}

/**
 * @brief 按照当前参数和运行状态更新本模块的行为或显示结果。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::showMessage(const QString& title, const QString& text, QMessageBox::StandardButtons buttons)
{
    QMetaObject::invokeMethod(qApp, [title, text, buttons]() {
        QMessageBox* msgBox = new QMessageBox(qApp->activeWindow());
        msgBox->setWindowTitle(title);
        msgBox->setText(text);
        msgBox->setStandardButtons(buttons);
        msgBox->setWindowModality(Qt::NonModal);
        msgBox->resize(300, 150);
        msgBox->show();
        if (buttons == QMessageBox::NoButton) {
            QTimer::singleShot(2000, msgBox, &QMessageBox::deleteLater);
        } else {
            QObject::connect(msgBox, &QMessageBox::finished, msgBox, &QMessageBox::deleteLater);
        }
    }, Qt::QueuedConnection);
}

/**
 * @brief 建立本功能需要的文件、设备、缓存或界面状态。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool Sava_data::createNewBinFile()
{
    QString binFileName = QString("%1_%2.bin").arg(m_baseFileName).arg(m_fileIndex);
    QString binFilePath = m_baseFolderPath + "/" + binFileName;

    closeFile();

    qint64 maxSingleWriteSize = static_cast<qint64>(m_rows) * static_cast<qint64>(m_cols) * sizeof(float);
    if (!checkDiskSpace(binFilePath, maxSingleWriteSize)) {
        return false;
    }

    m_binFile.setFileName(binFilePath);
    if (m_binFile.open(QIODevice::WriteOnly)) {
        m_binStream = new QDataStream(&m_binFile);
        m_binStream->setVersion(QDataStream::Qt_6_5);
        m_binStream->setFloatingPointPrecision(QDataStream::SinglePrecision);
        m_currentFileSize = 0;
        if (m_flushTimer.isValid()) {
            m_flushTimer.restart();
        } else {
            m_flushTimer.start();
        }
        return true;
    }

    emit showMessageSignal("错误", "无法创建新文件：" + binFilePath, QMessageBox::Ok);
    return false;
}

/**
 * @brief 建立本功能需要的文件、设备、缓存或界面状态。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
bool Sava_data::initializeSaveSessionLocked(const QString& saveType,
                                            int rows,
                                            int cols,
                                            int frequency,
                                            int extractCount,
                                            int start,
                                            int end,
                                            int differentialDistance)
{
    if (m_pendingRootFolderPath.isEmpty() || rows <= 0 || cols <= 0 || frequency <= 0) {
        emit showMessageSignal("错误", "保存参数无效，无法创建数据文件夹。", QMessageBox::Ok);
        return false;
    }

    closeFile();

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy_MM_dd_hh_mm_ss");
    const int safeExtract = qMax(1, extractCount);
    const int safeDiff = qMax(1, differentialDistance);
    const int startChannel = qMax(0, start);
    const int endChannel = qMax(startChannel, end);
    const double meterPerRawPoint = meterPerRawPointForFrequency(frequency);
    const double intervalDouble = qMax(0.0, meterPerRawPoint * safeExtract);
    const double pitchDouble = qMax(0.0, meterPerRawPoint * safeDiff);
    const double startDouble = startChannel * intervalDouble;
    const double endDouble = endChannel * intervalDouble;

    const QString paramStr = QString("_freq%1Hz_ext%2_diff%3_startCh%4_endCh%5_rows%6_cols%7_pitch%8m_interval%9m_startM%10_endM%11")
                                 .arg(frequency)
                                 .arg(safeExtract)
                                 .arg(safeDiff)
                                 .arg(startChannel)
                                 .arg(endChannel)
                                 .arg(rows)
                                 .arg(cols)
                                 .arg(pitchDouble, 0, 'f', 2)
                                 .arg(intervalDouble, 0, 'f', 2)
                                 .arg(startDouble, 0, 'f', 2)
                                 .arg(endDouble, 0, 'f', 2);

    const QString prefix = (saveType == "saveAll") ? QStringLiteral("data_all_") : QStringLiteral("data_");
    const QString folderName = prefix + timestamp + paramStr;
    const QString newFolderPath = m_pendingRootFolderPath + "/" + folderName;

    QDir saveDir;
    if (!saveDir.mkpath(newFolderPath)) {
        emit showMessageSignal("错误", "无法创建文件夹：" + newFolderPath, QMessageBox::Ok);
        return false;
    }

    m_fileIndex = 0;
    m_currentFileSize = 0;
    m_baseFolderPath = newFolderPath;
    m_baseFileName = prefix + timestamp;

    if (!createNewBinFile()) {
        return false;
    }

    emit showMessageSignal("提示",
                           (saveType == "saveAll" ? QStringLiteral("开始保存全部数据：")
                                                    : QStringLiteral("开始保存单点数据：")) + newFolderPath,
                           QMessageBox::Ok);
    return true;
}

/**
 * @brief 按当前保存策略写入或保留数据，并沿用既有的格式和错误处理。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::Preserve_rebuild_data(float* array, int rows, int cols, int frequency, int extractCount, int start, int end, int differentialDistance)
{
    if (!array || rows <= 0 || cols <= 0) {
        return;
    }

    const qint64 totalSize = static_cast<qint64>(rows) * static_cast<qint64>(cols) * static_cast<qint64>(sizeof(float));
    if (totalSize <= 0 || totalSize > std::numeric_limits<int>::max()) {
        return;
    }

    QByteArray snapshot(reinterpret_cast<const char*>(array), static_cast<int>(totalSize));
    Preserve_rebuild_data_snapshot(snapshot,
                                   true,
                                   -1,
                                   rows,
                                   cols,
                                   frequency,
                                   extractCount,
                                   start,
                                   end,
                                   differentialDistance);
}

/**
 * @brief 按当前保存策略写入或保留数据，并沿用既有的格式和错误处理。
 * @details 此实现属于采集数据保存界面与文件写入层；输入有效性、缓存容量、线程/信号边界均由原有代码维护。
 *          以下说明不改变二进制格式、算法结果、异常处理或任何控制流。
 */
void Sava_data::Preserve_rebuild_data_snapshot(const QByteArray& snapshot,
                                               bool fullFrame,
                                               int snapshotRow,
                                               int rows,
                                               int cols,
                                               int frequency,
                                               int extractCount,
                                               int start,
                                               int end,
                                               int differentialDistance)
{
    if (snapshot.isEmpty() || rows <= 0 || cols <= 0) {
        return;
    }

    QElapsedTimer saveTimer;
    saveTimer.start();
    const bool hadSaveWork = m_saveAllFlag || m_saveSingleFlag;
    auto emitSaveTime = [&]() {
        if (hadSaveWork) {
            emit saveProcessingTime(saveTimer.elapsed(), frequency);
        }
    };

    m_frequency = frequency;
    m_array = nullptr;
    m_rows = rows;
    m_cols = cols;

    const double meterPerRawPoint = meterPerRawPointForFrequency(frequency);

    const int safeExtract = qMax(1, extractCount);
    const int safeDiff = qMax(1, differentialDistance);
    const int startChannel = qMax(0, start);
    const int endChannel = qMax(startChannel, end);

    interval = meterPerRawPoint * safeExtract;
    Pitch = meterPerRawPoint * safeDiff;
    m_start = startChannel * interval;
    m_end = endChannel * interval;

    if (m_saveAllFlag) {
        QMutexLocker locker(&m_fileMutex);
        if (!m_binStream || !m_binFile.isWritable()) {
            if (!initializeSaveSessionLocked(QStringLiteral("saveAll"),
                                             rows,
                                             cols,
                                             frequency,
                                             extractCount,
                                             start,
                                             end,
                                             differentialDistance)) {
                m_saveAllFlag = false;
                ParameterManager::instance().setParameter("save_all_active", false);
                emit saveAllStateChanged(false);
                emitSaveTime();
                return;
            }
        }
        if (m_binStream && m_binFile.isWritable()) {
            const qint64 totalSize = static_cast<qint64>(rows) * static_cast<qint64>(cols) * sizeof(float);
            if (!fullFrame || snapshot.size() < totalSize || totalSize > std::numeric_limits<int>::max()) {
                qWarning() << "[SaveAll][SKIP] invalid snapshot"
                           << "fullFrame=" << fullFrame
                           << "snapshotBytes=" << snapshot.size()
                           << "expectedBytes=" << totalSize;
                emitSaveTime();
                return;
            }

            if (!checkDiskSpace(m_binFile.fileName(), totalSize)) {
                m_saveAllFlag = false;
                ParameterManager::instance().setParameter("save_all_active", false);
                emit saveAllStateChanged(false);
                closeFile();
                emitSaveTime();
                return;
            }

            const qint64 MAX_FILE_SIZE = 2LL * 1024 * 1024 * 1024;
            if (m_currentFileSize + totalSize > MAX_FILE_SIZE) {
                m_fileIndex++;
                if (!createNewBinFile()) {
                    m_saveAllFlag = false;
                    ParameterManager::instance().setParameter("save_all_active", false);
                    emit saveAllStateChanged(false);
                    emitSaveTime();
                    return;
                }
            }

            const int written = m_binStream->writeRawData(snapshot.constData(), static_cast<int>(totalSize));
            if (written != totalSize) {
                qWarning() << "[SaveAll][WRITE_ERROR]"
                           << "expectedBytes=" << totalSize
                           << "writtenBytes=" << written
                           << "status=" << m_binStream->status();
                m_saveAllFlag = false;
                ParameterManager::instance().setParameter("save_all_active", false);
                emit saveAllStateChanged(false);
                closeFile();
                emitSaveTime();
                return;
            }
            m_currentFileSize += totalSize;
            if (!m_flushTimer.isValid() || m_flushTimer.elapsed() >= 5000) {
                m_binFile.flush();
                m_flushTimer.restart();
            }
        } else {
            qWarning() << "Cannot save all data: m_binStream=" << (m_binStream ? "valid" : "nullptr")
                       << ", file writable=" << (m_binFile.isWritable() ? "true" : "false");
        }
    }

    if (m_saveSingleFlag) {
        const int requestedMonitorPosition = m_lockedSingleMonitorPosition;
        if (rows > 0) {
            const int monitorPosition = fullFrame
                                            ? qBound(0, requestedMonitorPosition, rows - 1)
                                            : qBound(0, snapshotRow, rows - 1);
            if (monitorPosition != requestedMonitorPosition) {
                qWarning() << "[SingleSave][ROW_CLAMP]"
                           << "requestedRow=" << requestedMonitorPosition
                           << "clampedRow=" << monitorPosition
                           << "rows=" << rows;
            }
            QMutexLocker locker(&m_fileMutex);
            if (!m_binStream || !m_binFile.isWritable()) {
                if (!initializeSaveSessionLocked(QStringLiteral("saveSingle"),
                                                 rows,
                                                 cols,
                                                 frequency,
                                                 extractCount,
                                                 start,
                                                 end,
                                                 differentialDistance)) {
                    m_saveSingleFlag = false;
                    m_lockedSingleMonitorPosition = -1;
                    ParameterManager::instance().setParameter("save_single_active", false);
                    ParameterManager::instance().setParameter("active_single_save_row", -1);
                    emit singleSaveStateChanged(false);
                    emitSaveTime();
                    return;
                }
            }
            if (m_binStream && m_binFile.isWritable()) {
                QElapsedTimer singleSaveTimer;
                singleSaveTimer.start();
                const QString tsStart = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");

                const qint64 singleDataSize = static_cast<qint64>(cols) * sizeof(float);

                if (!checkDiskSpace(m_binFile.fileName(), singleDataSize)) {
                    m_saveSingleFlag = false;
                    m_lockedSingleMonitorPosition = -1;
                    ParameterManager::instance().setParameter("save_single_active", false);
                    ParameterManager::instance().setParameter("active_single_save_row", -1);
                    emit singleSaveStateChanged(false);
                    closeFile();
                    emitSaveTime();
                    return;
                }

                const qint64 MAX_FILE_SIZE = 2LL * 1024 * 1024 * 1024;
                if (m_currentFileSize + singleDataSize > MAX_FILE_SIZE) {
                    m_fileIndex++;
                    if (!createNewBinFile()) {
                        m_saveSingleFlag = false;
                        m_lockedSingleMonitorPosition = -1;
                        ParameterManager::instance().setParameter("save_single_active", false);
                        ParameterManager::instance().setParameter("active_single_save_row", -1);
                        emit singleSaveStateChanged(false);
                        emitSaveTime();
                        return;
                    }
                }

                const int bytesToWrite = static_cast<int>(singleDataSize);
                const char* dataToWrite = nullptr;
                if (fullFrame) {
                    const qint64 rowOffset = static_cast<qint64>(monitorPosition) * singleDataSize;
                    if (snapshot.size() < rowOffset + singleDataSize) {
                        qWarning() << "[SingleSave][SKIP] invalid full-frame snapshot"
                                   << "snapshotBytes=" << snapshot.size()
                                   << "rowOffset=" << rowOffset
                                   << "rowBytes=" << singleDataSize;
                        emitSaveTime();
                        return;
                    }
                    dataToWrite = snapshot.constData() + rowOffset;
                } else {
                    if (snapshot.size() < singleDataSize) {
                        qWarning() << "[SingleSave][SKIP] invalid row snapshot"
                                   << "snapshotBytes=" << snapshot.size()
                                   << "rowBytes=" << singleDataSize;
                        emitSaveTime();
                        return;
                    }
                    dataToWrite = snapshot.constData();
                }

                const int written = m_binStream->writeRawData(dataToWrite, bytesToWrite);
                if (written != bytesToWrite) {
                    qWarning() << "[SingleSave][WRITE_ERROR]"
                               << "ts=" << tsStart
                               << "row=" << monitorPosition
                               << "cols=" << cols
                               << "expectedBytes=" << bytesToWrite
                               << "writtenBytes=" << written
                               << "status=" << m_binStream->status();
                    m_saveSingleFlag = false;
                    m_lockedSingleMonitorPosition = -1;
                    ParameterManager::instance().setParameter("save_single_active", false);
                    ParameterManager::instance().setParameter("active_single_save_row", -1);
                    emit singleSaveStateChanged(false);
                    closeFile();
                    emitSaveTime();
                    return;
                }
                m_currentFileSize += singleDataSize;
                if (!m_flushTimer.isValid() || m_flushTimer.elapsed() >= 5000) {
                    m_binFile.flush();
                    m_flushTimer.restart();
                }
                const qint64 elapsedMs = singleSaveTimer.elapsed();
                if (elapsedMs > 10) {
                    qDebug() << "[SingleSave][SLOW]"
                             << "start=" << tsStart
                             << "elapsedMs=" << elapsedMs
                             << "row=" << monitorPosition
                             << "cols=" << cols
                             << "bytes=" << singleDataSize;
                }
            } else {
                qWarning() << "Cannot save single point data: m_binStream=" << (m_binStream ? "valid" : "nullptr")
                           << ", file writable=" << (m_binFile.isWritable() ? "true" : "false");
            }
        } else {
            qWarning() << "[SingleSave][SKIP]"
                       << "invalid rows"
                       << "requestedMonitorPosition=" << requestedMonitorPosition
                       << "rows=" << rows;
        }
    }

    emitSaveTime();
}
