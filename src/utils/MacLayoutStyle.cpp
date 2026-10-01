// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MacLayoutStyle.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>

namespace {
    // Paints the tab widget's base line itself, in place of Qt's document-mode
    // paint event (which only draws it under the corner widgets). The tab bar
    // paints on top: the selected tab opens onto its page.
    class FullTabBase : public QObject {
    public:
        using QObject::QObject;

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() != QEvent::Paint) {
                return false;
            }
            auto *tabs = static_cast<QTabWidget *>(watched);
            const QTabBar *bar = tabs->tabBar();
            if (!bar->isVisible()) {
                return true;
            }
            // Colours from the tab bar: it always follows the current palette.
            QStyleOptionTabBarBase base;
            base.initFrom(bar);
            base.shape = bar->shape();
            const int overlap = tabs->style()->pixelMetric(QStyle::PM_TabBarBaseOverlap, nullptr, bar);
            base.rect = QRect(0, bar->geometry().bottom() - overlap + 1, tabs->width(), overlap);
            base.tabBarRect = bar->geometry();
            base.selectedTabRect = bar->tabRect(bar->currentIndex()).translated(bar->pos());
            QPainter painter(tabs);
            tabs->style()->drawPrimitive(QStyle::PE_FrameTabBarBase, &base, &painter, bar);
            return true;
        }
    };

    bool isCenteredTabBar(const QWidget *bar) {
        return bar->objectName() == QLatin1String("mainTabBar") || bar->objectName() == QLatin1String("centeredTabBar");
    }
}

int MacLayoutStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                              QStyleHintReturn *returnData) const {
    if (hint == SH_FormLayoutLabelAlignment) {
        return Qt::AlignLeft | Qt::AlignVCenter;
    }
    if (hint == SH_TabBar_Alignment && widget) {
        // Asked by the tab bar itself or by the tab widget that holds it.
        const auto *tabs = qobject_cast<const QTabWidget *>(widget);
        const QWidget *bar = tabs ? tabs->tabBar() : widget;
        if (bar && isCenteredTabBar(bar)) {
            return Qt::AlignCenter;
        }
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

void MacLayoutStyle::polish(QWidget *widget) {
    QProxyStyle::polish(widget);
    if (qobject_cast<QAbstractButton *>(widget) || qobject_cast<QComboBox *>(widget)
        || qobject_cast<QLineEdit *>(widget) || qobject_cast<QAbstractSpinBox *>(widget)) {
        widget->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    }
}

void MacLayoutStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                                 const QWidget *widget) const {
    QProxyStyle::drawControl(element, option, painter, widget);
    if (element == CE_MenuBarEmptyArea || element == CE_MenuBarItem) {
        // Linux / Windows, where the menu bar sits in the window: Fusion
        // draws a line under it, right above the line of the main tabs.
        // Paint it over with the window colour.
        const QRect r = widget ? widget->rect() : option->rect;
        const int y = qMin(option->rect.bottom(), r.bottom());
        painter->fillRect(QRect(option->rect.left(), y, option->rect.width(), 1), option->palette.color(QPalette::Window));
    }
    if (element == CE_TabBarTabShape) {
        // The selected tab: the window colour inside its outline, so it opens
        // onto its page instead of standing out lighter.
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (tab && (tab->state & State_Selected)) {
            painter->fillRect(tab->rect.adjusted(1, 1, -1, 0), tab->palette.color(QPalette::Window));
        }
    }
}

void MacLayoutStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                                   const QWidget *widget) const {
    if (element == PE_Frame && widget && widget->inherits("PayToEdit")) {
        // Same frame as the QLineEdit fields of the form.
        QProxyStyle::drawPrimitive(PE_FrameLineEdit, option, painter, widget);
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void MacLayoutStyle::drawFullTabBase(QTabWidget *tabs) {
    // Same as CoinPicker, whose line shows on every platform: the tab bar
    // draws none, its parent draws it all.
    tabs->tabBar()->setDrawBase(false);
    tabs->installEventFilter(new FullTabBase(tabs));
}
