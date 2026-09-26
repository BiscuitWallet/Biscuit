// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PixelIcons.h"

#include <QColor>
#include <QHash>
#include <QImage>

namespace PixelIcons {

namespace {
    const char *const hourglassRows[16] = {
        "..kkkkkkkkkkkk..",
        "..kLLLLLLLLLLk..",
        "..kkkkkkkkkkkk..",
        "...kgssssssgk...",
        "...kgssssssgk...",
        "....kgssssgk....",
        ".....kgssgk.....",
        "......kssk......",
        "......kgsk......",
        ".....kggsgk.....",
        "....kgggsggk....",
        "...kgggssgggk...",
        "...kgssssssgk...",
        "..kkkkkkkkkkkk..",
        "..kLLLLLLLLLLk..",
        "..kkkkkkkkkkkk..",
    };

    const char *const modemRows[16] = {
        "................",
        "................",
        "................",
        "................",
        "....kkkkkkkkkkk.",
        "...kcccccccccck.",
        "..kcccccccccck..",
        ".kkkkkkkkkkkkk..",
        ".kddddddddddddk.",
        ".kd1d2d3ddddddk.",
        ".kddddddddddddk.",
        ".kkkkkkkkkkkkkk.",
        "..kk........kk..",
        "................",
        "................",
        "................",
    };

    const char *const computerRows[16] = {
        "................",
        "..kkkkkkkkkkkk..",
        "..kcccccccccck..",
        "..kckkkkkkkkck..",
        "..kckbbbbbbkck..",
        "..kckbwbbbbkck..",
        "..kckbbbbbbkck..",
        "..kckbbbbbbkck..",
        "..kckkkkkkkkck..",
        "..kcccccccccck..",
        "..kkkkkkkkkkkk..",
        ".....kcccck.....",
        "...kkkkkkkkkk...",
        "..kddddddddddk..",
        "..kkkkkkkkkkkk..",
        "................",
    };

    const char *const networkRows[16] = {
        "kkkkkk..........",
        "kbbbbk..........",
        "kbwbbk..........",
        "kkkkkk..........",
        "..kk............",
        ".kkkk...........",
        "..y.............",
        "..y.............",
        "..yyyyyyyyyyy...",
        "............y...",
        "..........kkkkkk",
        "..........kbbbbk",
        "..........kbwbbk",
        "..........kkkkkk",
        "............kk..",
        "...........kkkk.",
    };

    const char *const clockRows[16] = {
        ".....kkkkkk.....",
        "...kkwwwwwwkk...",
        "..kwwwwkwwwwwk..",
        ".kwwwwwkwwwwwwk.",
        ".kwwwwwkwwwwwwk.",
        "kwwwwwwkwwwwwwwk",
        "kwwwwwwkwwwwwwwk",
        "kkwwwwwrkkkkwwkk",
        "kwwwwwwwwwwwwwwk",
        "kwwwwwwwwwwwwwwk",
        ".kwwwwwwwwwwwwk.",
        ".kwwwwwwwwwwwwk.",
        "..kwwwwwkwwwwk..",
        "...kkwwwwwwkk...",
        ".....kkkkkk.....",
        "................",
    };

    const char *const sendRows[16] = {
        "................",
        "................",
        "................",
        "kkkkkkkkkkkk....",
        "kwkwwwwwwkwk....",
        "kwwkwwwwkwwk.a..",
        "kwwwkwwkwwwk.aa.",
        "kwwwwkkwwwwkaaaa",
        "kwwwwwwwwwwk.aa.",
        "kwwwwwwwwwwk.a..",
        "kwwwwwwwwwwk....",
        "kkkkkkkkkkkk....",
        "................",
        "................",
        "................",
        "................",
    };

    const char *const receiveRows[16] = {
        "......aaaa......",
        "......aaaa......",
        "......aaaa......",
        "......aaaa......",
        "...aaaaaaaaaa...",
        "....aaaaaaaa....",
        ".....aaaaaa.....",
        "......aaaa......",
        ".......aa.......",
        "................",
        "kk............kk",
        "kdk..........kdk",
        "kddkkkkkkkkkkddk",
        "kddddddddddddddk",
        "kkkkkkkkkkkkkkkk",
        "................",
    };

    QPixmap draw(const char *const rows[16], const QHash<char, QColor> &colors) {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                image.setPixelColor(x, y, colors.value(rows[y][x], QColor(Qt::transparent)));
            }
        }
        // Nearest-neighbour upscale: crisp pixels at 2x.
        QPixmap pixmap = QPixmap::fromImage(image.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation));
        pixmap.setDevicePixelRatio(2.0);
        return pixmap;
    }

    const QColor outline(40, 40, 40);
}

QPixmap hourglass() {
    return draw(hourglassRows, {{'k', outline}, {'L', QColor(205, 205, 205)}, {'g', QColor(240, 240, 240)},
                                {'s', QColor(135, 135, 135)}});
}

QPixmap modem(bool connected, int phase) {
    const QColor off(110, 96, 70), green(70, 220, 80), red(232, 72, 50);
    auto led = [&](int index) {
        if (connected) return green;
        return phase % 3 == index ? (index == 2 ? red : green) : off;
    };
    return draw(modemRows, {{'k', outline}, {'c', QColor(236, 226, 200)}, {'d', QColor(205, 192, 160)},
                            {'1', led(0)}, {'2', led(1)}, {'3', led(2)}});
}

QIcon computer() {
    return QIcon(draw(computerRows, {{'k', outline}, {'c', QColor(222, 216, 200)}, {'d', QColor(190, 182, 162)},
                                     {'b', QColor(0, 128, 128)}, {'w', QColor(200, 240, 240)}}));
}

QIcon network() {
    return QIcon(draw(networkRows, {{'k', outline}, {'b', QColor(0, 128, 128)}, {'w', QColor(200, 240, 240)},
                                    {'y', QColor(230, 160, 40)}}));
}

QIcon history() {
    return QIcon(draw(clockRows, {{'k', outline}, {'w', QColor(255, 251, 232)}, {'r', QColor(200, 50, 40)}}));
}

QIcon send() {
    return QIcon(draw(sendRows, {{'k', outline}, {'w', QColor(255, 251, 232)}, {'a', QColor(47, 123, 214)}}));
}

QIcon receive() {
    return QIcon(draw(receiveRows, {{'k', outline}, {'d', QColor(205, 192, 160)}, {'a', QColor(63, 174, 74)}}));
}

}
