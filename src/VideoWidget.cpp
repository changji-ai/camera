#include "VideoWidget.h"

#include <QPainter>

#include "coco_labels.h"

namespace {

QColor classColor(int id)
{
    // deterministic palette around the color wheel
    return QColor::fromHsv((id * 47) % 360, 200, 255);
}

} // namespace

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(480, 320);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void VideoWidget::setFrame(const QImage &frame)
{
    m_frame = frame;
    if (!frame.isNull())
        m_frameSize = frame.size();
    update();
}

void VideoWidget::setDetections(const std::vector<Detection> &dets)
{
    m_detections = dets;
    update();
}

void VideoWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (m_frame.isNull())
        return;

    // letterbox-fit the frame into the widget
    const QSizeF scaled = m_frame.size().scaled(rect().size(), Qt::KeepAspectRatio);
    const QRectF target((width() - scaled.width()) / 2.0, (height() - scaled.height()) / 2.0,
                        scaled.width(), scaled.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, m_frame);

    // overlay detections (detections are in frame pixel coordinates)
    const double sx = target.width() / m_frame.width();
    const double sy = target.height() / m_frame.height();

    QFont f = p.font();
    f.setBold(true);
    f.setPointSizeF(std::max(9.0, target.height() / 38.0));
    p.setFont(f);

    for (const Detection &d : m_detections) {
        const QColor color = classColor(d.classId);
        const QRectF box(target.x() + d.box.left() * sx, target.y() + d.box.top() * sy,
                         d.box.width() * sx, d.box.height() * sy);

        p.setPen(QPen(color, std::max(2.0, target.height() / 300.0)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box);

        const QString label = QStringLiteral("%1 %2%")
                                  .arg(d.classId >= 0 && d.classId < kCocoLabelCount
                                           ? kCocoLabels[d.classId]
                                           : "?")
                                  .arg(int(d.score * 100));
        const QFontMetricsF fm(f);
        const QRectF textRect = fm.boundingRect(label).adjusted(-6, -3, 6, 3)
                                     .translated(box.left(), box.top() - fm.height());
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRect(textRect);
        p.setPen(Qt::black);
        p.drawText(textRect, Qt::AlignCenter, label);
    }
}
