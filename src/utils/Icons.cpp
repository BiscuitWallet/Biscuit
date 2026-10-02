// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "Icons.h"

#include <QHash>

#include "widgets/PixelIcons.h"

Icons* Icons::m_instance(nullptr);

Icons::Icons()
= default;

QIcon Icons::icon(const QString& name)
{
    QIcon icon = m_iconCache.value(name);

    if (!icon.isNull()) {
        return icon;
    }

    // Biscuit: these images are drawn as pixel icons (widgets/PixelIcons).
    static const QHash<QString, const char *> pixelIcons = {
        {"warning", "warning"}, {"warning.png", "warning"},
        {"info2", "info"}, {"info2.svg", "info"},
        {"lock", "lock"}, {"lock.svg", "lock"}, {"unlock.svg", "unlock"},
        {"seed", "seed"}, {"seed.png", "seed"},
        {"bitcoin.png", "bitcoin"}, {"litecoin.png", "litecoin"},
    };
    if (auto it = pixelIcons.constFind(name); it != pixelIcons.constEnd()) {
        icon = PixelIcons::icon(it.value());
    } else {
        icon = QIcon{":/assets/images/" + name};
    }

    m_iconCache.insert(name, icon);
    return icon;
}

Icons* Icons::instance()
{
    if (!m_instance) {
        m_instance = new Icons();
    }

    return m_instance;
}
