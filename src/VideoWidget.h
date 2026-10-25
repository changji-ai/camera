#pragma once

#include <QImage>
#include <QWidget>
#include <vector>

#include "YoloDetector.h"

// Draws the latest camera frame with detection overlays.
class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);            // called ~30fps from the capture thread signal
    void setDetections(const std::vector<Detection> &dets); // result of the latest inference

    QSize sizeHint() const override { return QSize(960, 540); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_frame;
    QSize m_frameSize;
    std::vector<Detection> m_detections;
};
