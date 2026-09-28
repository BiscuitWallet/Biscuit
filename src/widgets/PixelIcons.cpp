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

    const char *const phoneRows[32] = {
        "................................",
        ".........kkkkkkkkkk.............",
        "........khhhhhhhhhhkkkk.........",
        "......kkhyyyyyyyyyyyhhhk........",
        ".....khhyyyyyyyyyyyyyyyhkk......",
        "....khyyyyyYYYYkYYYyyyyyhhkk....",
        "....khyyyYkkkkkykkkkYYyyyyhk....",
        "...khyyyYkyyyyyyyyyykkYyyyyhk...",
        "...khyyYkyyyyyyyyyyyyykYYyyyhk..",
        "..khyyyYkyyyyyyyyyyyyyykYyyyhhk.",
        "..khyyYYkyyykkyyyyyyyyyykYyyyhk.",
        "..kyyyYkyykdd5ddkyyyyyyyykYyyhk.",
        "...kyykkydd44556dkyyyyyyykYyyhk.",
        "...kkkyyk33dddd6ddyyyyyyDkyyYDk.",
        "...khyyydd3dyyyd7dkyyyyDDkyyYDk.",
        "..khyyyyd22dyyyd7dkyyyDDDkyyYDDk",
        ".khyyyyykd11ddd88dyyyDDDDkyyYDDk",
        ".kyyyyyyyd11dddddkyyDDDDDkyyYDDk",
        ".kYYyyyyyykdddddkyDDDDDDkkyyYDDk",
        ".kYYYYYyyyyykkyyyDDDDDDk.kyyYDDk",
        ".kYYYYYYYyyyyyyyDDDDDDk...kyYDk.",
        ".kYYYYYYYYYYyyyDDDDDDk.....kkk..",
        ".kkYYYYYYYYYYYDDDDDDk...........",
        "...kkYYYYYYYYYDDDDDk............",
        ".....kkYYYYYYYDDDkk.............",
        ".......kkkYYYYDDk...............",
        "..........kkYYDk................",
        "............kkk.................",
        "................................",
        "................................",
        "................................",
        "................................",
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

    template <int N>
    QPixmap draw(const char *const (&rows)[N], const QHash<char, QColor> &colors) {
        QImage image(N, N, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                image.setPixelColor(x, y, colors.value(rows[y][x], QColor(Qt::transparent)));
            }
        }
        // Nearest-neighbour upscale: crisp pixels at 2x.
        QPixmap pixmap = QPixmap::fromImage(image.scaled(2 * N, 2 * N, Qt::IgnoreAspectRatio, Qt::FastTransformation));
        pixmap.setDevicePixelRatio(2.0);
        return pixmap;
    }

    const QColor outline(40, 40, 40);
}

QPixmap hourglass() {
    return draw(hourglassRows, {{'k', outline}, {'L', QColor(205, 205, 205)}, {'g', QColor(240, 240, 240)},
                                {'s', QColor(135, 135, 135)}});
}

QPixmap phone(bool connected, int phase) {
    // A yellow rotary desk phone in 3/4 view: handset arched over the base,
    // its end resting down the right side. While it dials, the finger holes
    // light up one after another; connected, they all stay lit.
    const QColor lit(255, 255, 255), dim(214, 172, 40);
    QHash<char, QColor> colors = {{'k', outline}, {'h', QColor(255, 244, 170)}, {'y', QColor(245, 212, 64)},
                                  {'Y', QColor(214, 172, 40)}, {'D', QColor(160, 122, 24)}, {'d', QColor(90, 68, 18)}};
    for (int i = 0; i < 8; ++i) {
        colors.insert(char('1' + i), connected || phase % 8 == i ? lit : dim);
    }
    return draw(phoneRows, colors);
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
