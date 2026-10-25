#include "MainWindow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "VideoWidget.h"
#include "coco_labels.h"

// ---------------------------------------------------------------- worker

DetectorWorker::DetectorWorker(QObject *parent)
    : QObject(parent)
{
}

DetectorWorker::~DetectorWorker() = default;

void DetectorWorker::loadModel(const QString &path)
{
    const bool ok = m_detector.load(path);
    emit modelLoaded(ok, ok ? QString() : m_detector.lastError());
}

void DetectorWorker::processFrame(QImage frame, float confThresh, float iouThresh)
{
    QElapsedTimer t;
    t.start();
    const std::vector<Detection> dets =
        m_detector.isLoaded() ? m_detector.detect(frame, confThresh, iouThresh)
                              : std::vector<Detection>{};
    emit detectionsReady(dets, int(t.elapsed()));
}

// ---------------------------------------------------------------- window

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Camera AI - 本地实时识别 (YOLO11n · ONNX Runtime)"));
    resize(1100, 760);

    // --- top controls
    auto *top = new QWidget;
    auto *layout = new QHBoxLayout(top);
    layout->setContentsMargins(8, 6, 8, 6);

    layout->addWidget(new QLabel(tr("摄像头:"), this));
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setMinimumWidth(220);
    layout->addWidget(m_deviceCombo);

    m_startStop = new QPushButton(tr("开始"), this);
    m_startStop->setFixedWidth(88);
    layout->addWidget(m_startStop);

    layout->addSpacing(16);
    layout->addWidget(new QLabel(tr("置信度:"), this));
    m_confSlider = new QSlider(Qt::Horizontal, this);
    m_confSlider->setRange(5, 95);
    m_confSlider->setValue(int(m_confThresh * 100));
    m_confSlider->setFixedWidth(180);
    layout->addWidget(m_confSlider);
    m_confLabel = new QLabel(QStringLiteral("0.35"), this);
    m_confLabel->setFixedWidth(36);
    layout->addWidget(m_confLabel);

    layout->addSpacing(16);
    m_modelLabel = new QLabel(tr("(未加载模型)"), this);
    layout->addWidget(m_modelLabel, 1);
    m_browseModel = new QPushButton(tr("选择模型..."), this);
    layout->addWidget(m_browseModel);

    // --- center
    m_videoWidget = new VideoWidget(this);

    auto *central = new QWidget;
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(top);
    centralLayout->addWidget(m_videoWidget, 1);
    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);

    // --- worker thread
    m_worker = new DetectorWorker;
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &MainWindow::requestLoad, m_worker, &DetectorWorker::loadModel);
    connect(this, &MainWindow::requestDetect, m_worker, &DetectorWorker::processFrame);
    connect(m_worker, &DetectorWorker::modelLoaded, this, &MainWindow::onModelLoaded);
    connect(m_worker, &DetectorWorker::detectionsReady, this,
            &MainWindow::onDetectionsReady);
    m_workerThread.start(QThread::LowPriority);

    // --- capture plumbing
    m_session.setVideoSink(&m_sink);
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &MainWindow::onFrameChanged,
            Qt::QueuedConnection);

    // --- signals
    connect(m_startStop, &QPushButton::clicked, this, &MainWindow::onStartStop);
    connect(m_confSlider, &QSlider::valueChanged, this, &MainWindow::onConfidenceChanged);
    connect(m_browseModel, &QPushButton::clicked, this, &MainWindow::browseModel);
    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { if (m_running) { stopCamera(); startCamera(); } });
    m_devices = new QMediaDevices(this); // per-instance object in Qt 6
    connect(m_devices, &QMediaDevices::videoInputsChanged, this,
            &MainWindow::populateDevices);

    populateDevices();

    auto *statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, [this] {
        m_displayFps = m_frameCount;
        m_inferFps = m_inferCount;
        m_frameCount = m_inferCount = 0;
        refreshStatus();
    });
    statusTimer->start(1000);

    // load default model
    m_modelPath = defaultModelPath();
    m_modelLabel->setText(QFileInfo(m_modelPath).fileName() + tr("（加载中...）"));
    emit requestLoad(m_modelPath);
}

MainWindow::~MainWindow()
{
    stopCamera();
    m_workerThread.quit();
    m_workerThread.wait(3000);
}

QString MainWindow::defaultModelPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString name = QStringLiteral("yolo11n.onnx");
    const QStringList candidates = {
        appDir + QStringLiteral("/models/") + name,
        appDir + QStringLiteral("/../Resources/models/") + name, // macOS bundle
        appDir + QStringLiteral("/../models/") + name,           // dev build tree
        QStringLiteral("models/") + name,
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return candidates.first();
}

void MainWindow::populateDevices()
{
    const int prev = m_deviceCombo->currentIndex();
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();
    const auto inputs = QMediaDevices::videoInputs();
    for (const QCameraDevice &dev : inputs)
        m_deviceCombo->addItem(dev.description());
    if (m_deviceCombo->count() == 0)
        m_deviceCombo->addItem(tr("（未检测到摄像头）"));
    if (prev >= 0 && prev < m_deviceCombo->count())
        m_deviceCombo->setCurrentIndex(prev);
    m_deviceCombo->blockSignals(false);
    m_startStop->setEnabled(m_deviceCombo->count() > 1 || (m_deviceCombo->count() == 1 && !m_deviceCombo->itemText(0).startsWith(QStringLiteral("（"))));
}

void MainWindow::onStartStop()
{
    if (m_running)
        stopCamera();
    else
        startCamera();
}

void MainWindow::startCamera()
{
    if (m_deviceCombo->count() == 0)
        return;
    const auto inputs = QMediaDevices::videoInputs();
    if (inputs.isEmpty())
        return;

    m_camera = new QCamera(inputs.at(m_deviceCombo->currentIndex()), this);
    m_session.setCamera(m_camera);
    m_camera->start();
    m_running = true;
    m_startStop->setText(tr("停止"));
    refreshStatus();
}

void MainWindow::stopCamera()
{
    if (m_camera) {
        m_camera->stop();
        m_session.setCamera(nullptr);
        m_camera->deleteLater();
        m_camera = nullptr;
    }
    if (m_running) {
        m_running = false;
        m_videoWidget->setDetections({});
        m_startStop->setText(tr("开始"));
        refreshStatus();
    }
}

void MainWindow::onModelLoaded(bool ok, const QString &error)
{
    m_modelReady = ok;
    if (ok) {
        m_modelLabel->setText(QFileInfo(m_modelPath).fileName() + tr("（就绪）"));
    } else {
        m_modelLabel->setText(QFileInfo(m_modelPath).fileName() + tr("（加载失败）"));
        statusBar()->showMessage(tr("模型加载失败：%1").arg(error), 8000);
    }
    refreshStatus();
}

void MainWindow::onDetectionsReady(const std::vector<Detection> &dets, int elapsedMs)
{
    m_worker->busy = false;
    m_inferCount++;
    m_lastInferMs = elapsedMs;
    m_lastDetections = dets;
    m_videoWidget->setDetections(dets);
}

void MainWindow::onFrameChanged(const QVideoFrame &frame)
{
    m_frameCount++;
    QImage img = frame.toImage();
    if (img.isNull())
        return;
    m_videoWidget->setFrame(img);

    // submit to the worker if it is idle (drop frames otherwise)
    if (m_modelReady && m_running && !m_worker->busy.load()) {
        m_worker->busy = true;
        emit requestDetect(img, m_confThresh, 0.45f);
    }
}

void MainWindow::onConfidenceChanged(int value)
{
    m_confThresh = value / 100.0f;
    m_confLabel->setText(QString::number(m_confThresh, 'f', 2));
}

void MainWindow::browseModel()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 ONNX 模型"), QString(), QStringLiteral("ONNX 模型 (*.onnx)"));
    if (path.isEmpty())
        return;
    m_modelPath = path;
    m_modelReady = false;
    m_modelLabel->setText(QFileInfo(path).fileName() + tr("（加载中...）"));
    emit requestLoad(path);
}

void MainWindow::refreshStatus()
{
    QStringList parts;
    parts << (m_running ? tr("摄像头：运行中") : tr("摄像头：已停止"));
    parts << (m_modelReady ? tr("模型：就绪") : tr("模型：未就绪"));
    parts << tr("推理：%1 ms · %2 fps").arg(m_lastInferMs).arg(m_inferFps);
    parts << tr("预览：%1 fps").arg(m_displayFps);

    // object summary (top classes)
    if (!m_lastDetections.empty()) {
        QMap<int, int> counts;
        for (const Detection &d : m_lastDetections)
            counts[d.classId]++;
        QList<QPair<int, int>> top;
        for (auto it = counts.begin(); it != counts.end(); ++it)
            top.append(qMakePair(it.key(), it.value()));
        std::sort(top.begin(), top.end(),
                  [](const QPair<int, int> &a, const QPair<int, int> &b) {
                      return a.second > b.second;
                  });
        QStringList names;
        const int n = qMin(3, int(top.size()));
        for (int i = 0; i < n; ++i) {
            const int cid = top[i].first;
            names << QStringLiteral("%1×%2")
                         .arg(cid >= 0 && cid < kCocoLabelCount ? kCocoLabels[cid] : "?")
                         .arg(top[i].second);
        }
        parts << tr("目标（%1）：%2").arg(m_lastDetections.size()).arg(names.join(QStringLiteral(", ")));
    }

    m_statusLabel->setText(parts.join(QStringLiteral("   |   ")));
}
