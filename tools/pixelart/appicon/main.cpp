// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Developer tool: builds the application icons from a 1:1 pixel-art sprite.
// Usage: appicon sprite.png outdir
// Writes outdir/{32..512}x{32..512}.png, appicon.ico and appicon.iconset/
// (turn it into appicon.icns with: iconutil -c icns outdir/appicon.iconset).
// Ivory rounded tile with a soft shadow, sprite scaled by an integer factor
// without smoothing so the pixels stay crisp.

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>

static QImage renderIcon(const QImage &sprite) {
    QImage icon(1024, 1024, QImage::Format_ARGB32_Premultiplied);
    icon.fill(Qt::transparent);
    QPainter p(&icon);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF tile(100, 100, 824, 824);
    for (int i = 12; i > 0; --i) {
        QPainterPath shadow;
        shadow.addRoundedRect(tile.translated(0, 10).adjusted(-i, -i, i, i), 185 + i, 185 + i);
        p.fillPath(shadow, QColor(0, 0, 0, 5));
    }
    QPainterPath shape;
    shape.addRoundedRect(tile, 185, 185);
    QLinearGradient g(tile.topLeft(), tile.bottomLeft());
    g.setColorAt(0, QColor("#fbf9f3"));
    g.setColorAt(1, QColor("#efebe0"));
    p.fillPath(shape, g);
    p.setPen(QPen(QColor(0, 0, 0, 30), 2));
    p.drawPath(shape);
    // Largest integer scale that fits ~72% of the tile.
    const int scale = std::max(1, int(824 * 0.72 / std::max(sprite.width(), sprite.height())));
    const QImage big = sprite.scaled(sprite.size() * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    p.drawImage(QPointF(512 - big.width() / 2.0, 512 - big.height() / 2.0 + 8), big);
    return icon;
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        qWarning("usage: appicon sprite.png outdir");
        return 1;
    }
    const QImage icon = renderIcon(QImage(argv[1]));
    const QDir out(argv[2]);
    out.mkpath("appicon.iconset");
    auto sized = [&](int s) { return icon.scaled(s, s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation); };
    for (int s : {32, 48, 64, 96, 128, 256, 512})
        sized(s).save(out.filePath(QString("%1x%1.png").arg(s)));
    for (int s : {16, 32, 128, 256, 512}) {
        sized(s).save(out.filePath(QString("appicon.iconset/icon_%1x%1.png").arg(s)));
        sized(s * 2).save(out.filePath(QString("appicon.iconset/icon_%1x%1@2x.png").arg(s)));
    }
    QImageWriter ico(out.filePath("appicon.ico"), "ico");
    if (!ico.write(sized(256))) {
        qWarning("ico: %s", qPrintable(ico.errorString()));
        return 1;
    }
    return 0;
}
