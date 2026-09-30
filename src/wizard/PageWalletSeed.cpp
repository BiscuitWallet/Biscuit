// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageWalletSeed.h"
#include "ui_PageWalletSeed.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>

#include "WalletWizard.h"
#include "constants.h"
#include "Seed.h"
#include "Icons.h"
#include "dialog/SeedDiceDialog.h"

PageWalletSeed::PageWalletSeed(WizardFields *fields, QWidget *parent)
    : QWizardPage(parent)
    , ui(new Ui::PageWalletSeed)
    , m_fields(fields)
{
    ui->setupUi(this);

    ui->frame_notice->setInfo(icons()->icon("seed"), "The following **16** words can be used to recover access to your wallet.\n\n"
                                                   "Biscuit uses **Polyseed**. For more information click **Help**.");

    ui->frame_invalidSeed->setInfo(icons()->icon("warning"), "Biscuit was unable to generate a valid seed.\n"
                                                             "This should never happen.\n"
                                                             "Please contact the developers immediately.");

    // Biscuit: the backup advice sits right under the words (no pop-up).
    m_seedAdvice = new QLabel("Write these words on paper, in order, and keep them offline. Never share them "
                              "and never type them on a website: anyone who has them can take your funds. "
                              "If you lose them, you lose access to your wallet.", this);
    m_seedAdvice->setWordWrap(true);
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->frame_seedDisplay) + 1, m_seedAdvice);

    QShortcut *shortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    QObject::connect(shortcut, &QShortcut::activated, [&](){
        SeedDiceDialog dialog{this};
        int r = dialog.exec();
        if (r == QDialog::Accepted) {
            if (!dialog.finished()) {
                this->onError();
                Utils::showError(this, "Unable to create polyseed using additional entropy", "Not enough entropy was collected", {"You have found a bug. Please contact the developers."});
                return;
            }

            this->generateSeed(dialog.getSecret());
            dialog.wipeSecret();
            Utils::showInfo(this, "Polyseed created successfully using additional entropy");
        }
    });

    connect(ui->btnRoulette, &QPushButton::clicked, [=]{
        this->seedRoulette(0);
    });
    connect(ui->btnCopy, &QPushButton::clicked, [this]{
        Utils::copyToClipboard(m_seed.mnemonic.join(" "));
    });
    connect(ui->btnOptions, &QPushButton::clicked, this, &PageWalletSeed::onOptionsClicked);
}

void PageWalletSeed::initializePage() {
    ui->frame_invalidSeed->hide();
    ui->frame_seedDisplay->show();
    m_seedAdvice->show();

    this->generateSeed();
    this->setTitle(m_fields->modeText);
}

void PageWalletSeed::seedRoulette(int count) {
    count += 1;
    if (count > m_rouletteSpin)
        return;

    this->generateSeed();

    QTimer::singleShot(10, [=] {
        this->seedRoulette(count);
    });
}

void PageWalletSeed::generateSeed(const char* secret) {
    QString mnemonic;

    m_seed = Seed(Seed::Type::POLYSEED, constants::networkType, "English", secret);
    mnemonic = m_seed.mnemonic.join(" ");
    m_restoreHeight = m_seed.restoreHeight;

    this->displaySeed(mnemonic);

    if (!m_seed.errorString.isEmpty()) {
        this->onError();
    }
}

void PageWalletSeed::displaySeed(const QString &seed){
    QStringList seedSplit = seed.split(" ");

    ui->seedWord1->setText(seedSplit[0]);
    ui->seedWord2->setText(seedSplit[1]);
    ui->seedWord3->setText(seedSplit[2]);
    ui->seedWord4->setText(seedSplit[3]);
    ui->seedWord5->setText(seedSplit[4]);
    ui->seedWord6->setText(seedSplit[5]);
    ui->seedWord7->setText(seedSplit[6]);
    ui->seedWord8->setText(seedSplit[7]);
    ui->seedWord9->setText(seedSplit[8]);
    ui->seedWord10->setText(seedSplit[9]);
    ui->seedWord11->setText(seedSplit[10]);
    ui->seedWord12->setText(seedSplit[11]);
    ui->seedWord13->setText(seedSplit[12]);
    ui->seedWord14->setText(seedSplit[13]);
    ui->seedWord15->setText(seedSplit[14]);
    ui->seedWord16->setText(seedSplit[15]);
}

void PageWalletSeed::onOptionsClicked() {
    QDialog dialog(this);
    dialog.setWindowTitle("Options");
    QVBoxLayout layout;
    QCheckBox checkbox("Extend this seed with a passphrase");
    checkbox.setChecked(m_fields->showSetSeedPassphrasePage);
    layout.addWidget(&checkbox);

    QDialogButtonBox buttons(QDialogButtonBox::Ok);
    layout.addWidget(&buttons);
    dialog.setLayout(&layout);
    connect(&buttons, &QDialogButtonBox::accepted, [&dialog]{
        dialog.close();
    });
    dialog.exec();
    m_fields->showSetSeedPassphrasePage = checkbox.isChecked();
}

void PageWalletSeed::onError() {
    ui->frame_invalidSeed->show();
    ui->frame_seedDisplay->hide();
    m_seedAdvice->hide();
    m_seedError = true;
    this->completeChanged();
}

int PageWalletSeed::nextId() const {
    // Biscuit: the Bitcoin/Litecoin seed comes next, then the usual path.
    return WalletWizard::Page_CoinSeed;
}

bool PageWalletSeed::validatePage() {
    if (m_seed.mnemonic.isEmpty()) {
        return false;
    }
    if (!m_restoreHeight) {
        return false;
    }

    m_fields->seed = m_seed;

    return true;
}

bool PageWalletSeed::isComplete() const {
    return !m_seedError;
}