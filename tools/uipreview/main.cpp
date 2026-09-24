// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Developer tool: renders a Qt Designer .ui file to a PNG, without the wallet.
// Usage: QT_QPA_PLATFORM=offscreen uipreview form.ui out.png [width] [style]
// Custom widgets are replaced by their base class: good enough to review layouts.

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QStyleFactory>
#include <QUiLoader>
#include <QWidget>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    if (argc < 3) {
        qWarning("usage: uipreview form.ui out.png [width] [style]");
        return 1;
    }
    if (argc > 4) {
        QApplication::setStyle(QStyleFactory::create(argv[4]));
    }

    QFile file(argv[1]);
    if (!file.open(QFile::ReadOnly)) {
        qWarning("cannot open %s", argv[1]);
        return 1;
    }
    QUiLoader loader;
    QWidget *widget = loader.load(&file);
    if (!widget) {
        qWarning("cannot load: %s", qPrintable(loader.errorString()));
        return 1;
    }

    if (qEnvironmentVariableIsSet("PREVIEW_FILL")) {
        // Worst case: a long Monero address in every empty field.
        const QString sample = "888tNkZrPN6JsEgekjMnABU4TBzc2Dt29EPAvkRxbANsAnjyPbb3iQ1YBRk1UXcdRsiKc9dhwMVgN5S9cQUiyoogDavup3H";
        for (auto *line : widget->findChildren<QLineEdit *>()) {
            if (line->text().isEmpty()) line->setText(sample);
        }
        for (auto *label : widget->findChildren<QLabel *>()) {
            if (label->text().isEmpty() && label->objectName() != "label_qr") label->setText("1.2345 XMR");
        }
    }

    const int width = argc > 3 ? QString(argv[3]).toInt() : 900;
    widget->resize(width, widget->heightForWidth(width) > 0 ? widget->heightForWidth(width) : widget->sizeHint().height());
    widget->resize(width, std::max(widget->height(), widget->minimumSizeHint().height()));
    widget->show();
    app.processEvents();
    return widget->grab().save(argv[2]) ? 0 : 1;
}
