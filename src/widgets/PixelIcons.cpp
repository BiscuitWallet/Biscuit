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
        ".....kkkkkkkkkk.",
        "....kcccccccccck",
        "...kcvvcvvcvvcdk",
        "..kcccccccccccdk",
        ".kccccccccccccdk",
        "kkkkkkkkkkkkkkdk",
        "kCCCCCCCCCCCCkdk",
        "kC1C2C3C4CCgCkk.",
        "kCCCCCCCCCCCCkk.",
        "kkkkkkkkkkkkkk..",
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
        "............kk..",
        ".kkkkkkkkkkkyyk.",
        ".kyyyyyyyyyyyyyk",
        ".kkkkkkkkkkkyyk.",
        "......kkkk..kk..",
        ".....kbggbk.....",
        "....kbgggbbk....",
        "....kbbgbbwk....",
        "....kbbbbggk....",
        "....kbgbbggk....",
        ".....kbbbgk.....",
        "..kk..kkkk......",
        ".kyykkkkkkkkkkk.",
        "kyyyyyyyyyyyyyk.",
        ".kyykkkkkkkkkkk.",
        "..kk............",
    };

    const char *const clockRows[16] = {
        "................",
        ".....kkkkkk.....",
        "...kkwwwwwwkk...",
        "..kwwwwkwwwwwk..",
        "..kwwwwkwwwwwk..",
        ".kwwwwwkwwwwwwk.",
        ".kwwwwwkwwwwwwk.",
        ".kkwwwwrkkkwwkk.",
        ".kwwwwwwwwwwwwk.",
        ".kwwwwwwwwwwwwk.",
        ".kwwwwwwwwwwwwk.",
        "..kwwwwwwwwwwk..",
        "..kwwwwkwwwwwk..",
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
        "kwwkwwwwkwwk....",
        "kwwwkwwkwwwk.a..",
        "kwwwwkkwwwwk.aa.",
        "kwwwwwwwwwwkaaaa",
        "kwwwwwwwwwwk.aa.",
        "kwwwwwwwwwwk.a..",
        "kwwwwwwwwwwk....",
        "kwwwwwwwwwwk....",
        "kkkkkkkkkkkk....",
        "................",
        "................",
    };

    const char *const receiveRows[16] = {
        "................",
        "......aaaa......",
        "......aaaa......",
        "......aaaa......",
        "....aaaaaaaa....",
        ".....aaaaaa.....",
        "......aaaa......",
        ".......aa.......",
        "................",
        ".kk..........kk.",
        ".kdk........kdk.",
        ".kddkkkkkkkkddk.",
        ".kddddddddddddk.",
        ".kkkkkkkkkkkkkk.",
        "................",
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
    // A beige dial-up modem seen from above, vents on top, lights in front:
    // they run while it dials and all stay lit once connected. Green: power.
    const QColor off(120, 40, 34), on(236, 58, 40);
    auto led = [&](int index) { return connected || phase % 4 == index ? on : off; };
    return draw(modemRows, {{'k', outline}, {'c', QColor(236, 229, 208)}, {'C', QColor(214, 204, 176)},
                            {'d', QColor(172, 160, 130)}, {'v', QColor(120, 110, 88)}, {'g', QColor(70, 190, 80)},
                            {'1', led(0)}, {'2', led(1)}, {'3', led(2)}, {'4', led(3)}});
}

QIcon computer() {
    return QIcon(draw(computerRows, {{'k', outline}, {'c', QColor(222, 216, 200)}, {'d', QColor(190, 182, 162)},
                                     {'b', QColor(0, 128, 128)}, {'w', QColor(200, 240, 240)}}));
}

QIcon network() {
    // The globe with two arrows: exchange around the world.
    return QIcon(draw(networkRows, {{'k', outline}, {'b', QColor(40, 110, 210)}, {'g', QColor(60, 170, 70)},
                                    {'w', Qt::white}, {'y', QColor(242, 211, 60)}}));
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
