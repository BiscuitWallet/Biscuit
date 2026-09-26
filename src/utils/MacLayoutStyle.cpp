// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MacLayoutStyle.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
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
