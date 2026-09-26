// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "RetroBusyBar.h"

#include <QApplication>
#include <QPainter>

namespace {
    constexpr int barHeight = 14;
    constexpr int blockWidth = 7;
    constexpr int blockGap = 2;
    constexpr int blockCount = 8;
    constexpr int step = 2;          // pixels per tick
    constexpr int tickMs = 16;       // ~125 px per second, smooth
}

RetroBusyBar::RetroBusyBar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_timer.setInterval(tickMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        // Back and forth between the two ends of the bar.
        const int group = blockCount * (blockWidth + blockGap) - blockGap;
        const int travel = qMax(0, width() - 4 - group);
        m_offset += m_direction * step;
        if (m_offset >= travel) {
            m_offset = travel;
            m_direction = -1;
        } else if (m_offset <= 0) {
            m_offset = 0;
            m_direction = 1;
        }
        update();
    });
}

QSize RetroBusyBar::sizeHint() const {
    return {200, barHeight};
}

void RetroBusyBar::setBusy(bool busy) {
    // Called on every progress update: only a change restarts the blocks,
    // otherwise they would jump back to the start before crossing the bar.
    if (busy == m_busy) {
        return;
    }
    m_busy = busy;
    m_offset = 0;
    m_direction = 1;
    busy ? m_timer.start() : m_timer.stop();
    update();
}

void RetroBusyBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    // The colours of the text fields (Biscuit's palette is set on their
    // class; a plain widget could get the system's colours on macOS).
    const QPalette pal = QApplication::palette("QLineEdit");
    // Sunken frame: dark top / left, light bottom / right.
    p.fillRect(rect(), pal.color(QPalette::Base));
    p.setPen(pal.color(QPalette::Mid));
    p.drawLine(r.topLeft(), r.topRight());
    p.drawLine(r.topLeft(), r.bottomLeft());
    p.setPen(pal.color(QPalette::Light));
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.topRight(), r.bottomRight());
    if (!m_busy) {
        return;
    }
    // The blocks, clipped to the inside of the frame.
    const QRect inside = rect().adjusted(2, 2, -2, -2);
    p.setClipRect(inside);
    const QColor block = pal.color(QPalette::Highlight);
    for (int i = 0; i < blockCount; ++i) {
        const int x = inside.left() + m_offset + i * (blockWidth + blockGap);
        p.fillRect(QRect(x, inside.top(), blockWidth, inside.height()), block);
    }
}
