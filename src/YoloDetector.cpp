#include "YoloDetector.h"

#include <QDebug>
#include <algorithm>
#include <array>
#include <cmath>

#include "ort_compat.h"
#include <onnxruntime_cxx_api.h>

struct YoloDetector::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "camera"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::string inputName;
    std::string outputName;
    bool loaded = false;
};

YoloDetector::YoloDetector()
    : m_impl(std::make_unique<Impl>())
{
}
YoloDetector::~YoloDetector() = default;

bool YoloDetector::isLoaded() const { return m_impl && m_impl->loaded; }

bool YoloDetector::load(const QString &onnxPath)
{
    m_lastError.clear();
    try {
        m_impl->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_impl->options.SetIntraOpNumThreads(0); // let ORT pick (all cores)

#ifdef _WIN32
        const std::wstring path = onnxPath.toStdWString();
        m_impl->session = std::make_unique<Ort::Session>(m_impl->env, path.c_str(), m_impl->options);
#else
        const QByteArray path = onnxPath.toUtf8();
        m_impl->session = std::make_unique<Ort::Session>(m_impl->env, path.constData(), m_impl->options);
#endif

        Ort::AllocatorWithDefaultOptions alloc;
        auto inName = m_impl->session->GetInputNameAllocated(0, alloc);
        auto outName = m_impl->session->GetOutputNameAllocated(0, alloc);
        m_impl->inputName = inName.get();
        m_impl->outputName = outName.get();

        // input shape: [N, C, H, W] (static export: 1x3x640x640)
        auto inShape = m_impl->session->GetInputTypeInfo(0)
                           .GetTensorTypeAndShapeInfo()
                           .GetShape();
        int size = 640;
        if (inShape.size() == 4 && inShape[3] > 0)
            size = static_cast<int>(inShape[3]);
        m_inputSize = size;

        // output shape: [N, 4+nc, anchors]
        auto outShape = m_impl->session->GetOutputTypeInfo(0)
                            .GetTensorTypeAndShapeInfo()
                            .GetShape();
        if (outShape.size() == 3 && outShape[1] > 4)
            m_numClasses = static_cast<int>(outShape[1]) - 4;

        m_inputBuffer.assign(static_cast<size_t>(3) * size * size, 0.447f /* 114/255 */);
        m_impl->loaded = true;
        return true;
    } catch (const Ort::Exception &e) {
        m_lastError = QString("onnxruntime: %1").arg(e.what());
    } catch (const std::exception &e) {
        m_lastError = QString::fromLocal8Bit(e.what());
    }
    m_impl->loaded = false;
    m_impl->session.reset();
    return false;
}

std::vector<Detection> YoloDetector::detect(const QImage &image, float confThresh,
                                            float iouThresh, int maxDetections)
{
    std::vector<Detection> results;
    if (!m_impl->loaded || image.isNull())
        return results;

    // ---------- 1. convert to packed RGB888 ----------
    QImage rgb = image;
    if (rgb.format() != QImage::Format_RGB888) {
        m_convertBuffer = rgb.convertToFormat(QImage::Format_RGB888);
        rgb = m_convertBuffer;
    }
    const int srcW = rgb.width(), srcH = rgb.height();
    if (srcW <= 0 || srcH <= 0)
        return results;
    const uchar *src = rgb.bits();
    const qsizetype stride = rgb.bytesPerLine();

    // ---------- 2. letterbox + bilinear resize into NCHW float ----------
    const int S = m_inputSize;
    const float scale = std::min(float(S) / float(srcW), float(S) / float(srcH));
    const int newW = std::max(1, int(std::round(srcW * scale)));
    const int newH = std::max(1, int(std::round(srcH * scale)));
    const float dx = (S - newW) * 0.5f;
    const float dy = (S - newH) * 0.5f;
    const float invScale = 1.0f / scale;

    std::vector<Detection> candidates;
    candidates.reserve(256);
    try {
        float *const chR = m_inputBuffer.data();
        float *const chG = chR + static_cast<size_t>(S) * S;
        float *const chB = chG + static_cast<size_t>(S) * S;

        for (int y = 0; y < S; ++y) {
            const float syy = (y - dy + 0.5f) * invScale - 0.5f;
            if (syy < -1.f || syy >= srcH) // fully in padding band
                continue;
            const float syc = std::min(std::max(syy, 0.f), float(srcH - 1));
            const int y0 = int(syc);
            const int y1 = std::min(y0 + 1, srcH - 1);
            const float fy = syc - float(y0);
            const uchar *row0 = src + qsizetype(y0) * stride;
            const uchar *row1 = src + qsizetype(y1) * stride;

            for (int x = 0; x < S; ++x) {
                const float sxx = (x - dx + 0.5f) * invScale - 0.5f;
                if (sxx < -1.f || sxx >= srcW) {
                    chR[qsizetype(y) * S + x] = chG[qsizetype(y) * S + x] =
                        chB[qsizetype(y) * S + x] = 0.447f;
                    continue;
                }
                const float sxc = std::min(std::max(sxx, 0.f), float(srcW - 1));
                const int x0 = int(sxc);
                const int x1 = std::min(x0 + 1, srcW - 1);
                const float fx = sxc - float(x0);
                const float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy);
                const float w01 = (1 - fx) * fy, w11 = fx * fy;
                const qsizetype p00 = qsizetype(x0) * 3, p10 = qsizetype(x1) * 3;
                for (int c = 0; c < 3; ++c) {
                    const float v = row0[p00 + c] * w00 + row0[p10 + c] * w10
                                  + row1[p00 + c] * w01 + row1[p10 + c] * w11;
                    (c == 0 ? chR : c == 1 ? chG : chB)[qsizetype(y) * S + x] = v * (1.f / 255.f);
                }
            }
        }

        // ---------- 3. run inference ----------
        const std::array<int64_t, 4> inShape{1, 3, S, S};
        auto tensor = Ort::Value::CreateTensor<float>(
            m_impl->memoryInfo, m_inputBuffer.data(), m_inputBuffer.size(),
            inShape.data(), inShape.size());

        const char *inputNames[] = {m_impl->inputName.c_str()};
        const char *outputNames[] = {m_impl->outputName.c_str()};
        auto output = m_impl->session->Run(Ort::RunOptions{nullptr},
                                           inputNames, &tensor, 1,
                                           outputNames, 1);

        // ---------- 4. parse [1, 4+nc, A] ----------
        const auto outInfo = output[0].GetTensorTypeAndShapeInfo();
        const auto outShape = outInfo.GetShape();
        if (outShape.size() != 3) {
            m_lastError = "unexpected output rank";
            return results;
        }
        const int nc = static_cast<int>(outShape[1]) - 4;
        const int anchors = static_cast<int>(outShape[2]);
        const float *out = output[0].GetTensorData<float>();

        const float invScore = confThresh;
        for (int a = 0; a < anchors; ++a) {
            const float *p = out + a; // column: stride anchors between elements of one channel
            // best class
            int best = -1;
            float bestScore = 0.f;
            for (int c = 0; c < nc; ++c) {
                const float s = p[static_cast<qsizetype>(4 + c) * anchors];
                if (s > bestScore) { bestScore = s; best = c; }
            }
            if (best < 0 || bestScore < invScore)
                continue;
            // cx,cy,w,h in letterboxed input pixels
            const float cx = p[0], cy = p[1], w = p[2], h = p[3];
            float x1 = (cx - w * 0.5f - dx) * invScale;
            float y1 = (cy - h * 0.5f - dy) * invScale;
            float x2 = (cx + w * 0.5f - dx) * invScale;
            float y2 = (cy + h * 0.5f - dy) * invScale;
            x1 = std::min(std::max(x1, 0.f), float(srcW - 1));
            y1 = std::min(std::max(y1, 0.f), float(srcH - 1));
            x2 = std::min(std::max(x2, 0.f), float(srcW - 1));
            y2 = std::min(std::max(y2, 0.f), float(srcH - 1));
            if (x2 <= x1 || y2 <= y1)
                continue;
            Detection d;
            d.box = QRectF(x1, y1, x2 - x1, y2 - y1);
            d.classId = best;
            d.score = bestScore;
            candidates.push_back(d);
        }
    } catch (const Ort::Exception &e) {
        m_lastError = QString("onnxruntime: %1").arg(e.what());
        return results;
    }

    // ---------- 5. per-class NMS ----------
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection &a, const Detection &b) { return a.score > b.score; });
    std::vector<char> suppressed(candidates.size(), 0);
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i])
            continue;
        const Detection &ki = candidates[i];
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (suppressed[j] || candidates[j].classId != ki.classId)
                continue;
            const QRectF &b = candidates[j].box;
            const float ix1 = std::max(float(ki.box.left()), float(b.left()));
            const float iy1 = std::max(float(ki.box.top()), float(b.top()));
            const float ix2 = std::min(float(ki.box.right()), float(b.right()));
            const float iy2 = std::min(float(ki.box.bottom()), float(b.bottom()));
            const float iw = std::max(0.f, ix2 - ix1), ih = std::max(0.f, iy2 - iy1);
            const float inter = iw * ih;
            const float unionArea = float(ki.box.width() * ki.box.height())
                                  + float(b.width() * b.height()) - inter;
            if (unionArea > 0 && inter / unionArea > iouThresh)
                suppressed[j] = 1;
        }
        results.push_back(ki);
        if (int(results.size()) >= maxDetections)
            break;
    }
    return results;
}
