// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "PixelIcons.h"

#include <QColor>
#include <QHash>
#include <QImage>

namespace PixelIcons {

namespace {
#include "PixelIcons16.inc"

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
        "...kkkkkkkkkk...",
        "..kyyyyyyyyyyk..",
        "..kyykkkkkkyyk..",
        "..kkk......kkk..",
        "....kkkkkkkk....",
        "...kyyyyyyyyk...",
        "..kyyyykkyyyyk..",
        "..kyyykwwkyyyk..",
        "..kyyyykkyyyyk..",
        ".kyyyyyyyyyyyyk.",
        ".kkkkkkkkkkkkkk.",
        ".kggggggggggggk.",
        ".kg1g2g3g4ggggk.",
        ".kGGGGGGGGGGGGk.",
        "..kkkkkkkkkkkk..",
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

    // size: logical pixels (16, or 32 for twice as big), sharp at 2x.
    QPixmap draw(const char *const rows[16], const QHash<char, QColor> &colors, int size = 16) {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                image.setPixelColor(x, y, colors.value(rows[y][x], QColor(Qt::transparent)));
            }
        }
        // Nearest-neighbour upscale: crisp pixels at 2x.
        QPixmap pixmap = QPixmap::fromImage(image.scaled(size * 2, size * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation));
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
    // A yellow desk phone on a grey modem, seen from the front; the modem's
    // lights run while it dials and all stay lit once connected.
    const QColor off(120, 40, 34), on(236, 58, 40);
    auto led = [&](int index) { return connected || phase % 4 == index ? on : off; };
    return draw(modemRows, {{'k', outline}, {'y', QColor(242, 211, 60)}, {'w', Qt::white},
                            {'g', QColor(201, 201, 201)}, {'G', QColor(138, 138, 138)},
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

QPixmap pixmap(const char *name, int size) {
    for (const Icon16 &entry : icons16) {
        if (qstrcmp(name, entry.name) == 0) {
            QHash<char, QColor> colors{{'k', outline}};
            for (const auto &c : entry.colors) {
                if (!c.key) {
                    break;
                }
                colors.insert(c.key, QColor::fromRgb(c.rgb));
            }
            return draw(entry.rows, colors, size);
        }
    }
    return {};
}

QIcon icon(const char *name) {
    static QHash<QByteArray, QIcon> cache;
    const QByteArray key(name);
    if (auto it = cache.constFind(key); it != cache.constEnd()) {
        return *it;
    }
    const QPixmap small = pixmap(name, 16);
    if (small.isNull()) {
        return {};
    }
    // 16 px, and 32 px for the places that show icons bigger (info frames):
    // pixels doubled, never smoothed.
    QIcon result(small);
    result.addPixmap(pixmap(name, 32));
    return cache.insert(key, result).value();
}

}
