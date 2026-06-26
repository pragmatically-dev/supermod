#include "../ChiplinksBackend.hpp"

#include <QImage>
#include <QThread>
#include <QMetaObject>
#include <QFile>
#include <QUrl>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>

#ifdef __ARM_NEON__
#  include <arm_neon.h>
#endif

extern "C" {
    void ci_getFramebufferInfo(void **addr, int *width, int *height,
                               int *type, int *bpl);
}

#define FBSPY_TYPE_RGB565 1
#define FBSPY_TYPE_RGBA   2

static void rgb565_to_argb32_row(const uint16_t *src, uint32_t *dst, int n)
{
#ifdef __ARM_NEON__
    const uint16x8_t mask_r = vdupq_n_u16(0xF800);
    const uint16x8_t mask_g = vdupq_n_u16(0x07E0);
    const uint16x8_t mask_b = vdupq_n_u16(0x001F);

    int i = 0;
    for (; i <= n - 8; i += 8) {
        uint16x8_t px = vld1q_u16(src + i);

        uint8x8_t r8 = vshrn_n_u16(vandq_u16(px, mask_r), 8);
        uint8x8_t g8 = vshrn_n_u16(vandq_u16(px, mask_g), 3);
        uint8x8_t b8 = vmovn_u16(vshlq_n_u16(vandq_u16(px, mask_b), 3));

        r8 = vorr_u8(r8, vshr_n_u8(r8, 5));
        g8 = vorr_u8(g8, vshr_n_u8(g8, 6));
        b8 = vorr_u8(b8, vshr_n_u8(b8, 5));

        uint8x8x4_t out;
        out.val[0] = b8;
        out.val[1] = g8;
        out.val[2] = r8;
        out.val[3] = vdup_n_u8(0xFF);
        vst4_u8(reinterpret_cast<uint8_t *>(dst + i), out);
    }
    for (; i < n; i++) {
        uint16_t p = src[i];
        uint8_t r = ((p >> 11) & 0x1F); r = (r << 3) | (r >> 2);
        uint8_t g = ((p >>  5) & 0x3F); g = (g << 2) | (g >> 4);
        uint8_t b = ( p        & 0x1F); b = (b << 3) | (b >> 2);
        dst[i] = (0xFFu << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
#else
    for (int i = 0; i < n; i++) {
        uint16_t p = src[i];
        uint8_t r = ((p >> 11) & 0x1F); r = (r << 3) | (r >> 2);
        uint8_t g = ((p >>  5) & 0x3F); g = (g << 2) | (g >> 4);
        uint8_t b = ( p        & 0x1F); b = (b << 3) | (b >> 2);
        dst[i] = (0xFFu << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
#endif
}

bool ChiplinksBackend::captureAreaAsPng(int rx, int ry, int rw, int rh,
                                        int centerX, int centerY) {

    if (m_captureInFlight.exchange(true)) {
        fprintf(stderr, "[supermod-capture] dropped: a capture is already in flight\n");
        return false;
    }

    void  *fbAddr = nullptr;
    int    fbW = 0, fbH = 0, fbType = 0, fbBpl = 0;
    ci_getFramebufferInfo(&fbAddr, &fbW, &fbH, &fbType, &fbBpl);

    if (!fbAddr || fbW <= 0 || fbH <= 0) {
        fprintf(stderr, "[supermod-capture] captureAreaAsPng: no framebuffer "
                        "(is framebuffer-spy loaded?)\n");
        m_captureInFlight.store(false);
        return false;
    }

    int x0 = std::max(0, rx);
    int y0 = std::max(0, ry);
    int x1 = std::min(fbW, rx + rw);
    int y1 = std::min(fbH, ry + rh);
    int cw = x1 - x0;
    int ch = y1 - y0;
    if (cw <= 2 || ch <= 2) {
        fprintf(stderr, "[supermod-capture] captureAreaAsPng: region too small\n");
        m_captureInFlight.store(false);
        return false;
    }

    fprintf(stderr, "[supermod-capture] crop %dx%d @ (%d,%d) from FB %dx%d type=%d\n",
            cw, ch, x0, y0, fbW, fbH, fbType);

    QImage img(cw, ch, QImage::Format_ARGB32);

    if (fbType == FBSPY_TYPE_RGBA) {

        for (int y = 0; y < ch; y++) {
            const uint8_t *src = (const uint8_t *)fbAddr
                                 + (y0 + y) * fbBpl + x0 * 4;
            memcpy(img.scanLine(y), src, cw * 4);
        }
    } else {
        for (int y = 0; y < ch; y++) {
            const uint16_t *src = (const uint16_t *)((const uint8_t *)fbAddr
                                  + (y0 + y) * fbBpl + x0 * 2);
            uint32_t *dst = (uint32_t *)img.scanLine(y);
            rgb565_to_argb32_row(src, dst, cw);
        }
    }

    const QString path    = QStringLiteral("/tmp/supermod_capture.png");
    const QString fileUrl = QStringLiteral("file:///tmp/supermod_capture.png");

    ChiplinksBackend *self = this;
    QThread *t = QThread::create([self, img, path, fileUrl, centerX, centerY]() {
        if (img.save(path, "PNG")) {
            fprintf(stderr, "[supermod-capture] saved %dx%d PNG\n",
                    img.width(), img.height());
            QMetaObject::invokeMethod(self, "captureReady",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, fileUrl),
                                      Q_ARG(int, centerX),
                                      Q_ARG(int, centerY));
        } else {
            fprintf(stderr, "[supermod-capture] failed to save PNG\n");
        }

        self->m_captureInFlight.store(false);
    });
    t->connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();

    return true;
}

void ChiplinksBackend::discardCaptureFile(const QString &fileUrl) {

    QString p = fileUrl;
    if (p.startsWith(QLatin1String("file://")))
        p = QUrl(p).toLocalFile();
    if (p.isEmpty() || !p.startsWith(QLatin1String("/tmp/supermod_capture")))
        return;
    if (QFile::remove(p))
        fprintf(stderr, "[supermod-capture] discarded temp file %s\n", p.toUtf8().constData());
}
