// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "CoinPicker.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>

#include "utils/Icons.h"

namespace biscuit::coins {

CoinPicker::CoinPicker(QWidget *parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addStretch();
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) {
            emit currentIndexChanged(id);
        }
    });
}

void CoinPicker::addCoin(const QIcon &icon, const QString &name) {
    auto *button = new QToolButton(this);
    button->setText(name);
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCheckable(true);
    button->setMinimumHeight(32);
    button->setMinimumWidth(110);
    button->setCursor(Qt::PointingHandCursor);
    const int id = m_group->buttons().size();
    m_group->addButton(button, id);
    auto *layout = static_cast<QHBoxLayout *>(this->layout());
    layout->insertWidget(layout->count() - 1, button);   // before the stretch
    if (id == 0) {
        button->setChecked(true);
    }
}

int CoinPicker::currentIndex() const {
    return m_group->checkedId();
}

void CoinPicker::setCurrentIndex(int index) {
    if (QAbstractButton *button = m_group->button(index)) {
        button->setChecked(true);
    }
}

void addWalletCoins(CoinPicker *picker) {
    picker->addCoin(QIcon(":/assets/images/appicons/monero.png"), "Monero");
    picker->addCoin(icons()->icon("bitcoin.png"), "Bitcoin");
    picker->addCoin(icons()->icon("litecoin.png"), "Litecoin");
}

}
