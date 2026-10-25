#pragma once

#include <QImage>
#include <QMetaType>
#include <QRect>
#include <QString>
#include <memory>
#include <vector>

struct Detection {
    QRectF box;       // in original image coordinates
    int classId = -1;
    float score = 0.f;
};

Q_DECLARE_METATYPE(std::vector<Detection>)

// Synchronous YOLO (ultralytics export, e.g. YOLO11n) detector on ONNX Runtime CPU.
// Preprocessing (letterbox + bilinear resize) and postprocessing (score filter,
// coordinate restore, per-class NMS) are implemented locally - no OpenCV needed.
class YoloDetector {
public:
    YoloDetector();
    ~YoloDetector();

    bool load(const QString &onnxPath);
    bool isLoaded() const;
    int inputSize() const { return m_inputSize; }

    // confThresh: minimum class score; iouThresh: NMS IoU threshold
    std::vector<Detection> detect(const QImage &image,
                                  float confThresh = 0.35f,
                                  float iouThresh = 0.45f,
                                  int maxDetections = 100);

    QString lastError() const { return m_lastError; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    int m_inputSize = 640;
    int m_numClasses = 80;
    QString m_lastError;

    std::vector<float> m_inputBuffer; // reused NCHW buffer
    QImage m_convertBuffer;           // reused RGB888 conversion buffer
};
