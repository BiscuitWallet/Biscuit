// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Appearance.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyleHints>
#include <QTabWidget>
#include <QtMath>

#include "utils/config.h"

namespace Appearance {

void styleTabs(QTabWidget *tabs, bool centered) {
    Q_UNUSED(centered)   // centering: MacLayoutStyle, by tab bar name
    // Fusion draws flat, square tabs that match the rest of the interface.
    tabs->setDocumentMode(true);
}

void apply() {
    const QString choice = conf()->get(Config::appearance).toString();
    auto *hints = QGuiApplication::styleHints();
    if (choice == "dark") {
        hints->setColorScheme(Qt::ColorScheme::Dark);
    } else if (choice == "light") {
        hints->setColorScheme(Qt::ColorScheme::Light);
    } else {
        hints->unsetColorScheme();   // follow the system
    }
}

bool isDark() {
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void toggle() {
    conf()->set(Config::appearance, isDark() ? "light" : "dark");
    apply();
}

QIcon toggleIcon() {
    // Drawn, so it follows the text colour of the current appearance.
    const qreal dpr = 2.0;
    QPixmap pixmap(QSize(16, 16) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = isDark();
    const QColor color = dark ? QColor(250, 214, 92) : QColor(90, 90, 110);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    if (dark) {
        // Sun: switch back to light.
        p.drawEllipse(QPointF(8, 8), 3.4, 3.4);
        p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4;
            p.drawLine(QPointF(8 + 5.2 * qCos(a), 8 + 5.2 * qSin(a)), QPointF(8 + 7.0 * qCos(a), 8 + 7.0 * qSin(a)));
        }
    } else {
        // Crescent moon: switch to dark.
        QPainterPath moon;
        moon.addEllipse(QPointF(8, 8), 6.2, 6.2);
        QPainterPath bite;
        bite.addEllipse(QPointF(11, 5.5), 5.4, 5.4);
        p.drawPath(moon.subtracted(bite));
    }
    return QIcon(pixmap);
}

}
