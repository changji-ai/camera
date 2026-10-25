// Headless smoke test: load the bundled model and run a full detect() pass on
// a synthetic image. Verifies model loading, preprocessing, inference and NMS
// without a camera - runs on every CI platform.
// Exit 0 = ok, 1 = failure.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QString>
#include <cstdio>

#include "../src/YoloDetector.h"
#include "../src/coco_labels.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // locate model: <bindir>/../../models/yolo11n.onnx or cwd models/
    QString model;
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/models/yolo11n.onnx"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../models/yolo11n.onnx"),
        QStringLiteral("models/yolo11n.onnx"),
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c)) {
            model = c;
            break;
        }
    }
    if (model.isEmpty()) {
        std::printf("FAIL: model not found\n");
        return 1;
    }

    YoloDetector det;
    if (!det.load(model)) {
        std::printf("FAIL: load: %s\n", det.lastError().toUtf8().constData());
        return 1;
    }
    std::printf("model loaded, input size %d\n", det.inputSize());

    // synthetic frame with a couple of clearly shaped objects on gray
    QImage img(1280, 720, QImage::Format_ARGB32);
    img.fill(QColor(60, 60, 60));
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(230, 40, 40));
        p.drawEllipse(500, 150, 260, 420); // roughly person-like vertical blob
        p.setBrush(QColor(40, 90, 230));
        p.drawRect(150, 500, 420, 150);    // roughly vehicle-like blob
    }

    const std::vector<Detection> dets = det.detect(img, 0.20f, 0.45f);
    std::printf("detections: %zu\n", dets.size());
    for (const Detection &d : dets)
        std::printf("  %s %.2f  [%.0f,%.0f %.0fx%.0f]\n",
                    (d.classId >= 0 && d.classId < kCocoLabelCount ? kCocoLabels[d.classId] : "?"),
                    d.score, d.box.left(), d.box.top(), d.box.width(), d.box.height());

    if (dets.empty()) {
        std::printf("FAIL: no detections on synthetic image (inference pipeline broken?)\n");
        return 1;
    }
    std::printf("SMOKE OK\n");
    return 0;
}
