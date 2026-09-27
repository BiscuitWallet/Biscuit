// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MacLayoutStyle.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOption>
#include <QTabWidget>

namespace {
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
    if (element == PE_FrameTabBarBase && widget && isCenteredTabBar(widget) && painter->device() != widget) {
        // Document-mode tab widget: the base line under its corner widget
        // (the appearance button) starts at the corner widget, leaving a gap
        // after the tab bar. Start it from the left edge instead.
        QStyleOptionTabBarBase base = *qstyleoption_cast<const QStyleOptionTabBarBase *>(option);
        base.rect.setLeft(0);
        QProxyStyle::drawPrimitive(element, &base, painter, widget);
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}
