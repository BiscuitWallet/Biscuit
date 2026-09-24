// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MacLayoutStyle.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>

int MacLayoutStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                              QStyleHintReturn *returnData) const {
    if (hint == SH_FormLayoutLabelAlignment) {
        return Qt::AlignLeft | Qt::AlignVCenter;
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
