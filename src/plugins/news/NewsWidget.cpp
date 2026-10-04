// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "NewsWidget.h"

#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "utils/Icons.h"
#include "utils/Utils.h"
#include "utils/WebsocketNotifier.h"

namespace {
    constexpr int UrlRole = Qt::UserRole;
}

NewsWidget::NewsWidget(QWidget *parent)
        : QWidget(parent)
        , m_list(new QTreeWidget(this))
        , m_open(new QPushButton("Read on biscuitwallet.com", this))
        , m_empty(new QLabel("No news yet.", this))
{
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({"Date", "Title"});
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setStretchLastSection(true);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);


    m_open->setIcon(icons()->icon("external-link.svg"));
    m_open->setAutoDefault(false);
    m_empty->setStyleSheet("color: gray;");

    auto *bottom = new QHBoxLayout;
    // The summary is a tooltip on the title: the list keeps the space.
    bottom->addStretch(1);
    bottom->addWidget(m_open);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_empty);
    layout->addWidget(m_list, 1);
    layout->addLayout(bottom);

    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &NewsWidget::showSelected);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &NewsWidget::openSelected);
    connect(m_open, &QPushButton::clicked, this, &NewsWidget::openSelected);

    connect(websocketNotifier(), &WebsocketNotifier::dataReceived, this, [this](const QString &type, const QJsonValue &json) {
        if (type == "news") {
            update(json.toArray());
        }
    });

    update({});
}

void NewsWidget::update(const QJsonArray &news) {
    const QString selectedUrl = m_list->currentItem() ? m_list->currentItem()->data(0, UrlRole).toString() : QString();
    m_list->clear();
    for (const QJsonValue &value : news) {
        const QJsonObject obj = value.toObject();
        auto *item = new QTreeWidgetItem(m_list);
        const QDate date = QDate::fromString(obj.value("date").toString(), Qt::ISODate);
        item->setText(0, QLocale().toString(date, QLocale::ShortFormat));
        item->setText(1, obj.value("title").toString());
        item->setData(0, UrlRole, obj.value("url").toString());
        item->setToolTip(1, obj.value("summary").toString().toHtmlEscaped());
        if (obj.value("pinned").toBool()) {
            // Kept on top by the feed: say why, instead of a date out of order.
            item->setText(0, tr("Pinned"));
            QFont bold = item->font(1);
            bold.setBold(true);
            item->setFont(1, bold);
        }
        if (item->data(0, UrlRole).toString() == selectedUrl) {
            m_list->setCurrentItem(item);
        }
    }
    if (!m_list->currentItem() && m_list->topLevelItemCount() > 0) {
        m_list->setCurrentItem(m_list->topLevelItem(0));
    }

    const bool any = m_list->topLevelItemCount() > 0;
    m_empty->setVisible(!any);
    m_list->setVisible(any);
    showSelected();
}

void NewsWidget::showSelected() {
    const QTreeWidgetItem *item = m_list->currentItem();
    m_open->setVisible(item);
}

void NewsWidget::openSelected() {
    if (const QTreeWidgetItem *item = m_list->currentItem()) {
        Utils::externalLinkWarning(this, item->data(0, UrlRole).toString());
    }
}
