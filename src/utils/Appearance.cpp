// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Appearance.h"

#include <QApplication>
#include <QGuiApplication>
#include <QStatusBar>
#include <QTabWidget>
#include <functional>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyleHints>
#include <QtMath>

#include "utils/config.h"

namespace Appearance {

namespace {
    // Re-applies `style` now and whenever light / dark changes.
    void keepStyled(QWidget *widget, const std::function<QString()> &style) {
        widget->setStyleSheet(style());
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, widget,
                         [widget, style] { widget->setStyleSheet(style()); });
    }
}

void styleTabs(QTabWidget *tabs, bool centered) {
    tabs->setDocumentMode(true);
    keepStyled(tabs, [centered] {
        const bool dark = isDark();
        const QString bg = dark ? "#2b2b2b" : "#ececec", text = dark ? "#c8c8c8" : "#3a3a3a",
                      sel = dark ? "#404040" : "#ffffff", selText = dark ? "#ffffff" : "#000000",
                      border = dark ? "#1e1e1e" : "#d2d2d2", hover = dark ? "#353535" : "#f6f6f6";
        return QString(R"(%1
QTabWidget::pane { border: none; border-top: 1px solid %6; }
QTabBar { background: %2; qproperty-drawBase: 0; }
QTabBar::tab { background: %2; color: %3; border: none; border-right: 1px solid %6; padding: 4px 20px; font-size: %8pt; }
QTabBar::tab:first { border-left: 1px solid %6; }
QTabBar::tab:selected { background: %4; color: %5; }
QTabBar::tab:hover:!selected { background: %7; }
)").arg(centered ? "QTabWidget::tab-bar { alignment: center; }" : "", bg, text, sel, selText, border, hover)
           .arg(QApplication::font().pointSize());
    });
}

void styleStatusBar(QStatusBar *bar) {
    keepStyled(bar, [] {
        const bool dark = isDark();
        return QString("QStatusBar { background: %1; border-top: 1px solid %3; } QStatusBar QLabel { color: %2; }")
                .arg(dark ? "#252525" : "#f4f4f4", dark ? "#e6e6e6" : "#1e1e1e", dark ? "#1a1a1a" : "#d2d2d2");
    });
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
