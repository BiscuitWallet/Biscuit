// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "DocsDialog.h"
#include "ui_DocsDialog.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QScrollBar>

#include "utils/Utils.h"
#include "ColorScheme.h"

DocsDialog::DocsDialog(QWidget *parent)
        : WindowModalDialog(parent)
        , ui(new Ui::DocsDialog)
{
    ui->setupUi(this);

    ui->toolButton_backward->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    ui->toolButton_forward->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));

    ui->splitter->setStretchFactor(1, 8);

    // Biscuit: the pages of biscuitwallet.com/docs, bundled (contrib/docs/pages). toc.txt
    // lists the sections and their pages, in reading order: "Section", then "  slug|Title".
    QTreeWidgetItem *section = nullptr;
    const QStringList toc = Utils::loadQrc(":/docs/toc.txt").split("\n");
    for (const QString &line : toc) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        if (!line.startsWith(" ")) {
            section = new QTreeWidgetItem(ui->index, {line.trimmed()});
            continue;
        }
        const QString slug = line.trimmed().section('|', 0, 0);
        const QString title = line.trimmed().section('|', 1);
        const QString resource = "qrc:/docs/" + slug + ".html";
        if (!section || !QFile::exists(":/docs/" + slug + ".html")) {
            continue;
        }
        auto *item = new QTreeWidgetItem(section, {title});
        item->setData(0, Qt::UserRole, resource);
        m_items[resource] = item;

        // Search looks in the text, not in the markup.
        QString text = Utils::loadQrc(":/docs/" + slug + ".html");
        text.remove(QRegularExpression("<[^>]*>"));
        m_docs[resource] = text.toLower();
    }
    ui->index->expandAll();

    connect(ui->index, &QTreeWidget::itemClicked, [this](QTreeWidgetItem *current, int column){
        if (ui->index->indexOfTopLevelItem(current) != -1) {
            current->setExpanded(!current->isExpanded());
            return;
        }

        QString resource = current->data(0, Qt::UserRole).toString();
        this->showDoc(resource);
    });

    connect(ui->textBrowser, &QTextBrowser::anchorClicked, [this](const QUrl &url){
        if (url.isRelative()) {
            ui->textBrowser->setSource(QUrl(m_currentSource).resolved(url));
            return;
        }
        Utils::externalLinkWarning(this, url.toString());
        this->showDoc(m_currentSource);
    });

    connect(ui->textBrowser, &QTextBrowser::sourceChanged, [this](const QUrl& source){
        this->updateHighlights(ui->search->text());

        QString newSource = source.toString();
        if (m_currentSource == newSource) {
            return;
        }
        m_currentSource = newSource;

        const QString page = source.toString(QUrl::RemoveFragment);
        if (m_items.contains(page)) {
            ui->index->setCurrentItem(m_items[page]);
        }
    });

    connect(ui->textBrowser, &QTextBrowser::backwardAvailable, [this](bool available){
       ui->toolButton_backward->setEnabled(available);
    });

    connect(ui->textBrowser, &QTextBrowser::forwardAvailable, [this](bool available){
       ui->toolButton_forward->setEnabled(available);
    });

    connect(ui->toolButton_backward, &QToolButton::clicked, [this]{
       ui->textBrowser->backward();
    });

    connect(ui->toolButton_forward, &QToolButton::clicked, [this]{
        ui->textBrowser->forward();
    });

    connect(ui->search, &QLineEdit::textEdited, [this](const QString &text){
        this->filterIndex(text);
        this->updateHighlights(ui->search->text());
    });

    // Pressing 'enter' in the search box shouldn't close the dialog
    QPushButton *closeButton = ui->buttonBox->button(QDialogButtonBox::Close);
    if (closeButton) {
        closeButton->setAutoDefault(false);
    }

    this->showDoc("index");
}

void DocsDialog::filterIndex(const QString &text) {
    QTreeWidgetItemIterator it(ui->index);
    while (*it) {
        QString resource = (*it)->data(0, Qt::UserRole).toString();
        bool docContainsText = m_docs[resource].contains(text, Qt::CaseInsensitive);
        bool titleContainsText = (*it)->text(0).contains(text, Qt::CaseInsensitive);

        if (titleContainsText && !text.isEmpty()) {
            ColorScheme::updateFromWidget(this);
            (*it)->setBackground(0, ColorScheme::YELLOW.asColor(true));
        } else {
            (*it)->setBackground(0, Qt::transparent);
        }

        if (docContainsText || titleContainsText) {
            (*it)->setHidden(false);

            QTreeWidgetItem *parent = (*it)->parent();
            if (parent) {
                parent->setHidden(false);
                parent->setExpanded(true);
            }
        } else {
            (*it)->setHidden(true);
        }
        ++it;
    }
}

void DocsDialog::showDoc(const QString &doc, const QString &highlight) {
    ColorScheme::updateFromWidget(this);
    QPalette p = qApp->palette();
    p.setBrush(QPalette::Link, ColorScheme::darkScheme ? ColorScheme::BLUE.asColor() : QColor("blue"));
    qApp->setPalette(p);

    QString resource = doc;

    if (!resource.startsWith("qrc")) {
        // Pages asked for under the names of Feather's documentation.
        static const QHash<QString, QString> pages{
            {"report_an_issue", "report-bug"},
            {"seed_scheme", "seed-types"},
            {"show_wallet_seed", "backups"},
            {"wallet_files", "wallet-files"},
            {"create_wallet_hardware_device", "hardware-wallets"},
            {"restore_height", "synchronization#restore-height"},
            {"synchronization", "synchronization"},
            {"balance", "balance"},
            {"offline_tx_signing", "offline-signing"},
            {"pay_to_many", "pay-to-many"},
            {"send_transaction", "send"},
        };
        const QString page = pages.value(doc, doc);
        const QString slug = page.section('#', 0, 0);
        const QString fragment = page.section('#', 1);
        resource = "qrc:/docs/" + slug + ".html" + (fragment.isEmpty() ? "" : "#" + fragment);
    }

    const QString page = resource.section('#', 0, 0);
    if (m_items.contains(page)) {
        ui->index->setCurrentItem(m_items[page]);
    }

    QString file = resource.section('#', 0, 0);
    file.remove("qrc");
    if (!QFile::exists(file)) {
        Utils::showError(this, "Unable to load document", "File does not exist");
        ui->textBrowser->setSource(m_currentSource);
        return;
    }

    ui->textBrowser->setSource(QUrl(resource));
}

void DocsDialog::updateHighlights(const QString &searchString, bool scrollToCursor) {
    QTextDocument *document = ui->textBrowser->document();
    QTextCursor cursor(document);

    // Clear highlighting
    QTextCursor cursor2(document);
    cursor2.select(QTextCursor::Document);
    QTextCharFormat defaultFormat;
    defaultFormat.setBackground(Qt::transparent);
    cursor2.mergeCharFormat(defaultFormat);

    bool firstFind = true;
    if (!searchString.isEmpty()) {
        while (!cursor.isNull() && !cursor.atEnd()) {
            cursor = document->find(searchString, cursor);

            if (!cursor.isNull()) {
                if (firstFind) {
                    // Scroll to first match
                    QRect cursorRect = ui->textBrowser->cursorRect(cursor);
                    int positionToScroll = ui->textBrowser->verticalScrollBar()->value() + cursorRect.top();
                    ui->textBrowser->verticalScrollBar()->setValue(positionToScroll);

                    firstFind = false;
                }

                ColorScheme::updateFromWidget(this);
                QTextCharFormat colorFormat(cursor.charFormat());
                colorFormat.setBackground(ColorScheme::YELLOW.asColor(true));
                cursor.mergeCharFormat(colorFormat);
            }
        }
    }
}

DocsDialog::~DocsDialog() = default;