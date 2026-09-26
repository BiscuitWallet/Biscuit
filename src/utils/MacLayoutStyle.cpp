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

int MacLayoutStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                              QStyleHintReturn *returnData) const {
    if (hint == SH_FormLayoutLabelAlignment) {
        return Qt::AlignLeft | Qt::AlignVCenter;
    }
    if (hint == SH_TabBar_Alignment && widget) {
        // Asked by the tab bar itself or by the tab widget that holds it.
        const auto *tabs = qobject_cast<const QTabWidget *>(widget);
        const QWidget *bar = tabs ? tabs->tabBar() : widget;
        if (bar && (bar->objectName() == QLatin1String("mainTabBar") || bar->objectName() == QLatin1String("centeredTabBar"))) {
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
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}
