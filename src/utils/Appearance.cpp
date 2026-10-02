// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "Appearance.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QPixmap>
#include <QStyleHints>
#include <QTabWidget>

#include "utils/MacLayoutStyle.h"
#include "utils/config.h"
#include "widgets/PixelIcons.h"

namespace Appearance {

void styleTabs(QTabWidget *tabs, bool centered) {
    Q_UNUSED(centered)   // centering: MacLayoutStyle, by tab bar name
    // Fusion draws flat, square tabs that match the rest of the interface.
    tabs->setDocumentMode(true);
    MacLayoutStyle::drawFullTabBase(tabs);
}

namespace {
    // Biscuit's own palettes: light is a soft ivory, dark a neutral grey.
    // Set on every widget class, since macOS hands some classes (combo
    // boxes, text edits, progress bars...) the system's colours otherwise.
    QPalette makePalette(bool dark) {
        const QColor window = dark ? QColor("#2b2b2b") : QColor("#f5f3ec");
        const QColor button = dark ? QColor("#353535") : QColor("#f5f3ec");
        const QColor base = dark ? QColor("#1f1f1f") : QColor("#fdfcf8");
        const QColor alternate = dark ? QColor("#262626") : QColor("#f5f3ec");
        const QColor text = dark ? QColor("#e6e6e6") : QColor("#1e1e1e");
        const QColor disabledText = dark ? QColor("#7a7a7a") : QColor("#9a978f");
        QPalette p(button, window);   // derives light, mid, dark and shadow
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::Button, button);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, alternate);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::PlaceholderText, dark ? QColor("#8c8c8c") : QColor("#8a877f"));
        p.setColor(QPalette::ToolTipBase, dark ? QColor("#3a3a3a") : QColor("#fffdf5"));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Highlight, QColor("#2f7bd6"));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, dark ? QColor("#6fb1ff") : QColor("#1a5fb4"));
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
            p.setColor(QPalette::Disabled, role, disabledText);
        }
        return p;
    }

    bool g_dark = false;

    // The user's choice decides; the system's scheme only counts when
    // following it. On Linux, the platform theme may not honour
    // setColorScheme(), so the scheme it reports can stay light.
    bool wantDark() {
        const QString choice = conf()->get(Config::appearance).toString();
        if (choice == "dark") {
            return true;
        }
        if (choice == "light") {
            return false;
        }
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }

    void applyPalette() {
        g_dark = wantDark();
        const QPalette palette = makePalette(g_dark);
        QApplication::setPalette(palette);
        for (const char *cls : {"QComboBox", "QAbstractItemView", "QListView", "QTreeView", "QTableView", "QHeaderView",
                                "QLineEdit", "QTextEdit", "QPlainTextEdit", "QAbstractSpinBox", "QProgressBar",
                                "QMenu", "QMenuBar", "QTabBar", "QPushButton", "QToolButton", "QGroupBox",
                                "QStatusBar", "QScrollBar", "QCheckBox", "QRadioButton", "QLabel"}) {
            QApplication::setPalette(palette, cls);
        }
    }
}

void apply() {
    static bool connected = false;
    if (!connected) {
        // The system switching light / dark (when following it) repaints too.
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] { applyPalette(); });
        connected = true;
    }
    const QString choice = conf()->get(Config::appearance).toString();
    auto *hints = QGuiApplication::styleHints();
    if (choice == "dark") {
        hints->setColorScheme(Qt::ColorScheme::Dark);
    } else if (choice == "light") {
        hints->setColorScheme(Qt::ColorScheme::Light);
    } else {
        hints->unsetColorScheme();   // follow the system
    }
    applyPalette();
}

bool isDark() {
    return g_dark;
}

void toggle() {
    conf()->set(Config::appearance, isDark() ? "light" : "dark");
    apply();
}

QIcon toggleIcon() {
    // The sun switches back to light, the moon to dark.
    return PixelIcons::icon(isDark() ? "sun" : "moon");
}

}
