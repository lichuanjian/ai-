/**
 * @file main.cpp
 * @brief 应用装配根：创建 Qt 应用、工作线程和实时数据处理信号链。
 *
 * 运行链路为：receive_data(PCIe DMA) -> Rebuild_data(差分/抽取) -> Unwrap(相位连续化)
 * -> Filter(高通与监听行提取) -> RMS、FFT、Audio/MainWindowData；保存支路在 Unwrap 后复制快照。
 */
#include <QApplication>
#include "mainwindow.h"
#include "pcie_fun.h"
#include "pcie_fun.c"
#include "audio.h"
#include <QAudioSink>
#include <QBuffer>
#include <QtMath>
#include <QAudioFormat>
#include <QDebug>
#include <QMediaDevices>
#include <QThread>
#include <cmath>
#include <fstream>
#include "receive_data.h"
//#include "my_time.h"
#include "Filter.h"
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMessageBox>
#include "rms_calculate.h"
#include "my.h"

#include <QIODevice>

#include <QWidget>
#include <QPainter>
#include <cmath>
#include <QLabel>
#include <QImage>
#include <QColor>

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QTimer>
#include <cstdlib>
#include <QIcon>
#include <cstddef>
#include <QMetaType>

#include "rebuild_data.h"
#include "fft_calculate.h"
#include <QElapsedTimer>
#include "unwrap.h"
#include "sava_data.h"



/**
 * @brief 初始化 Qt 运行环境、主窗口与退出清理链路。
 * @details 此实现属于应用程序启动入口；参数语义、线程边界及副作用均以现有函数体为准。
 *          本注释仅补充实现意图，不改变算法、资源所有权或执行顺序。
 */
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    qRegisterMetaType<unsigned char*>("unsigned char*");
    app.setApplicationName(QStringLiteral("啁啾扫频型分布式光纤测井设备"));
    app.setApplicationDisplayName(QStringLiteral("啁啾扫频型分布式光纤测井设备"));
    app.setWindowIcon(QIcon(":/app_icon.ico"));
    // 初始化PCIe设备，若失败则打开事件处理并输出错误信息
    if (pcie_init() < 0) {
        open_event();
        qDebug() << "pcie_init fail";
        // return -1;
    }
    // 创建并显示主窗口（适配拆分后的MainWindow）
    MainWindow w1;
    w1.setWindowIcon(QIcon(":/app_icon.ico"));
    w1.showMaximized();
           qDebug() << "初始化窗口";
    // 创建10个线程用于多任务处理（保留原有逻辑）
    QThread *thread1 = new QThread(&app);
    QThread *thread2 = new QThread(&app);
    QThread *thread3 = new QThread(&app);
    QThread *thread4 = new QThread(&app);
    QThread *thread5 = new QThread(&app);
    QThread *thread6 = new QThread(&app);
    QThread *thread7 = new QThread(&app);
    QThread *thread8 = new QThread(&app);
    QThread *thread9 = new QThread(&app);
    QThread *thread10 = new QThread(&app);

    // 创建各种功能对象（保留原有逻辑）
    Sava_data *sava_data = new Sava_data();
    Unwrap *unwrap = new Unwrap(); // 数据解包器
    Audio *audioProcessor = new Audio();// 音频处理器
    rms_calculate *Rms_calculate = new rms_calculate();// 方差计算器
    Rebuild_data *rebuild_data = new Rebuild_data();// 数据重构器
    FFT_Calculate *fft_calculator = new FFT_Calculate();// FFT计算器
    receive_data *receive_Data = new receive_data();// 自定义线程
    Filter *w = new Filter();// 自定义窗口部件

    // 检查默认音频输入设备是否存在（保留原有逻辑）
    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        QMessageBox::warning(nullptr, "音频", "还未接入音频设备，故无法播放声音");
        // return -1;
    }

    // 将对象移动到对应的线程中执行（保留原有逻辑）
    Rms_calculate->moveToThread(thread1);
    sava_data->moveToThread(thread2);
    audioProcessor->moveToThread(thread4);
    w->moveToThread(thread3);
    fft_calculator->moveToThread(thread7);
    rebuild_data->moveToThread(thread8);
    unwrap->moveToThread(thread9);
    receive_Data->moveToThread(thread10);

    // 连接信号与槽，构建数据处理流程（核心逻辑保留，适配新MainWindow的槽函数名）
    // 原始数据 -> 数据重构 -> 数据解包 -> 窗口部件处理 -> 方差计算 -> 主窗口显示RMS数据
    const auto rawDataSignal = static_cast<void (receive_data::*)(unsigned char*, int, int, int, int, int, int, int, int)>(&receive_data::Raw_data);
    const auto rebuildRawSlot = static_cast<void (Rebuild_data::*)(unsigned char*, int, int, int, int, int, int, int, int)>(&Rebuild_data::Receive_raw_data);
    QObject::connect(receive_Data, rawDataSignal, rebuild_data, rebuildRawSlot);
    QObject::connect(rebuild_data,
                     &Rebuild_data::widget_my_array_Signal,
                     unwrap,
                     &Unwrap::Receive_rebuild_data,
                     Qt::QueuedConnection);
    QObject::connect(unwrap,
                     &Unwrap::Unwrap_Data_SaveSnapshotSignal,
                     sava_data,
                     &Sava_data::Preserve_rebuild_data_snapshot,
                     Qt::QueuedConnection);
    QObject::connect(unwrap, &Unwrap::Unwrap_Data_Signal, w, &Filter::array_handleSignal);
    QObject::connect(w, &Filter::Filter_my_array_Signal, Rms_calculate, &rms_calculate::array_handleSignal);
    // 适配新MainWindow：RMS数据发送到MainWindow的onReceiveRMSData槽
    QObject::connect(Rms_calculate, &rms_calculate::sendDataToReceiver, &w1, &MainWindow::onReceiveRMSData);
    // 适配新MainWindow：RMS数据发送到MainWindow的onReceiveRMSData槽
      QObject::connect(w, &Filter::Data_after_time_Extraction, &w1, &MainWindow::onReceiveStrainData);


    // 窗口部件处理后的数据 -> 音频处理、FFT计算、主窗口显示音频数据
    QObject::connect(w, &Filter::array_tongguolvboSignal, audioProcessor, &Audio::array_handleSignal);
   QObject::connect(w, &Filter::array_tongguolvboSignal, fft_calculator, &FFT_Calculate::FFT_handleSignal);
    QObject::connect(w, &Filter::multiAudioRowsSignal, &w1, &MainWindow::onReceiveMultiAudioData);

    // FFT计算结果 -> 主窗口显示频谱数据（适配新MainWindow）
    QObject::connect(fft_calculator, &FFT_Calculate::FFT_OK_Signal, &w1, &MainWindow::onReceiveFFTData);

    // 连接各个处理模块与MainWindow的通信，让每个环节的计算时间能够显示
    QObject::connect(receive_Data, &receive_data::pcieProcessingTime, &w1, &MainWindow::onReceivePcieTime1);//原始数据传输过来的信号
    QObject::connect(rebuild_data, &Rebuild_data::totalProcessingTime,&w1, &MainWindow::onReceiveProcessingTime);//数据重构信号
    QObject::connect(unwrap, &Unwrap::preserveProcessingTime, &w1, &MainWindow::onPreserveProcessingTime);
    QObject::connect(w, &Filter::processingTimeMeasured, &w1, &MainWindow::onFilterProcessingTime);//数据滤波信号
    QObject::connect(Rms_calculate, &rms_calculate::rmsProcessingTime, &w1, &MainWindow::onRmsProcessingTime);
    QObject::connect(fft_calculator, &FFT_Calculate::fftProcessingTime, &w1, &MainWindow::onFftProcessingTime);
    QObject::connect(sava_data, &Sava_data::saveProcessingTime, &w1, &MainWindow::onSaveProcessingTime);

    // 发送到保存功能（保留原有逻辑，匹配新MainWindow的信号）
    QObject::connect(&w1, &MainWindow::save_only, sava_data, &Sava_data::onButtonClicked_saveonlyone);//将第几个音频通道发送到滤波模块
    QObject::connect(&w1, &MainWindow::save_all, sava_data, &Sava_data::onButtonClicked_saveall);//将第几个音频通道发送到滤波模块
    QObject::connect(&w1, &MainWindow::end_save, sava_data, &Sava_data::onButtonClicked_end_save);//将第几个音频通道发送到滤波模块
    QObject::connect(&w1, &MainWindow::end_save_all, sava_data, &Sava_data::onButtonClicked_end_save_all);//将第几个音频通道发送到滤波模块
    QObject::connect(&w1, &MainWindow::is_filter, unwrap, &Unwrap::Receive_is_filter);//将第几个音频通道发送到滤波模块
    QObject::connect(&w1, &MainWindow::is_filter, w, &Filter::Receive_is_filter);//将第几个音频通道发送到滤波模块
    QObject::connect(sava_data, &Sava_data::requestSelectFolder,&w1, &MainWindow::onRequestSelectFolder);  // 子线程→主线程：异步非阻塞
    // 3. 连接信号槽：主线程将选择结果返回给子线程
    QObject::connect(&w1, &MainWindow::folderSelectedToThread,sava_data, &Sava_data::onFolderSelected);
    QObject::connect(sava_data, &Sava_data::showMessageSignal, &w1, &MainWindow::showMessage);    // 主线程→子线程：异步非阻塞
    QObject::connect(sava_data, &Sava_data::freeGBtoUI, &w1, &MainWindow::onDiskSpaceUpdated);
    QObject::connect(sava_data, &Sava_data::singleSaveStateChanged, &w1, &MainWindow::onSingleSaveStateChanged);
    QObject::connect(sava_data, &Sava_data::saveAllStateChanged, &w1, &MainWindow::onSaveAllStateChanged);

    // 启动所有线程（保留原有逻辑）
    thread1->start();
    thread2->start();
    thread2->setPriority(QThread::LowPriority);
    thread3->start();
    thread3->setPriority(QThread::LowPriority);
    thread4->start();
    thread4->setPriority(QThread::HighPriority);
    thread5->start();
    thread5->setPriority(QThread::HighPriority);
    thread6->start();
    thread7->start();
    thread8->start();
    thread9->start();
    thread10->start();



    bool cleanupStarted = false;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        if (cleanupStarted) {
            return;
        }
        cleanupStarted = true;

        w1.beginShutdown();
        QObject::disconnect(nullptr, nullptr, &w1, nullptr);
        QObject::disconnect(&w1, nullptr, nullptr, nullptr);

        // 停止 receive_Data
        receive_Data->stop();

        // 停止其他线程
        thread1->quit();
        thread2->quit();//疑似冗余线程，暂未有内容
        thread3->quit();
        thread4->quit();
        thread5->quit();
        thread6->quit();
        thread7->quit();
        thread8->quit();
        thread9->quit();
        thread10->quit();

        // 等待线程结束，设置超时时间（例如 5 秒）
        const int timeout = 5000; // 5 秒
        if (!thread1->wait(timeout)) qDebug() << "thread1 failed to stop";
        if (!thread2->wait(timeout)) qDebug() << "thread2 failed to stop";
        if (!thread3->wait(timeout)) qDebug() << "thread3 failed to stop";
        if (!thread4->wait(timeout)) qDebug() << "thread4 failed to stop";
        if (!thread5->wait(timeout)) qDebug() << "thread5 failed to stop";
        if (!thread6->wait(timeout)) qDebug() << "thread6 failed to stop";
        if (!thread7->wait(timeout)) qDebug() << "thread7 failed to stop";
        if (!thread8->wait(timeout)) qDebug() << "thread8 failed to stop";
        if (!thread9->wait(timeout)) qDebug() << "thread9 failed to stop";
        if (!thread10->wait(timeout)) qDebug() << "thread10 failed to stop";

        // 确认线程已停止后再删除
        if (!thread1->isRunning() && !thread2->isRunning() && !thread3->isRunning() &&
            !thread4->isRunning() && !thread5->isRunning() && !thread6->isRunning() &&
            !thread7->isRunning() && !thread8->isRunning() && !thread9->isRunning() &&
            !thread10->isRunning()) {
            delete thread1;
            delete thread2;
            delete thread3;
            delete thread4;
            delete thread5;
            delete thread6;
            delete thread7;
            delete thread8;
            delete thread9;
            delete thread10;
        } else {
            qDebug() << "Some threads are still running, cannot delete safely";
        }

        //释放pcie资源
        pcie_deinit();

        // 清理其他资源
        delete unwrap;
        delete sava_data;
        delete audioProcessor;
        delete Rms_calculate;
        delete rebuild_data;
        delete fft_calculator;
        delete w;
        delete receive_Data;

    });

    // 进入应用程序主事件循环
    int ret = app.exec();
    return ret;
}


