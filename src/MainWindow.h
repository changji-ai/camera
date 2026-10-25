#pragma once

#include <QCamera>
#include <QImage>
#include <QMainWindow>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QThread>
#include <QVideoFrame>
#include <QVideoSink>
#include <atomic>

#include "YoloDetector.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class VideoWidget;

// Runs model loading + inference on its own thread.
class DetectorWorker : public QObject {
    Q_OBJECT
public:
    explicit DetectorWorker(QObject *parent = nullptr);
    ~DetectorWorker() override;

    std::atomic_bool busy{false}; // UI side checks & sets this to throttle submissions

public slots:
    void loadModel(const QString &path);
    void processFrame(QImage frame, float confThresh, float iouThresh);

signals:
    void modelLoaded(bool ok, const QString &error);
    void detectionsReady(const std::vector<Detection> &dets, int elapsedMs);

private:
    YoloDetector m_detector;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void requestLoad(const QString &path);
    void requestDetect(QImage frame, float confThresh, float iouThresh);

private slots:
    void populateDevices();
    void onStartStop();
    void onModelLoaded(bool ok, const QString &error);
    void onDetectionsReady(const std::vector<Detection> &dets, int elapsedMs);
    void onFrameChanged(const QVideoFrame &frame);
    void onConfidenceChanged(int value);
    void browseModel();
    void refreshStatus();

private:
    QString defaultModelPath() const;
    void startCamera();
    void stopCamera();

    // UI
    VideoWidget *m_videoWidget = nullptr;
    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_startStop = nullptr;
    QSlider *m_confSlider = nullptr;
    QLabel *m_confLabel = nullptr;
    QLabel *m_modelLabel = nullptr;
    QPushButton *m_browseModel = nullptr;
    QLabel *m_statusLabel = nullptr;

    // capture
    QMediaDevices *m_devices = nullptr;
    QMediaCaptureSession m_session;
    QVideoSink m_sink;
    QCamera *m_camera = nullptr;
    bool m_running = false;

    // inference
    QThread m_workerThread;
    DetectorWorker *m_worker = nullptr;
    bool m_modelReady = false;
    QString m_modelPath;
    float m_confThresh = 0.35f;

    // stats
    int m_frameCount = 0, m_displayFps = 0;
    int m_inferCount = 0, m_inferFps = 0;
    int m_lastInferMs = 0;
    std::vector<Detection> m_lastDetections;
};
