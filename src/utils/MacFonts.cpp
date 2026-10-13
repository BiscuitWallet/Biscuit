// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

#include "MacFonts.h"

#ifdef Q_OS_MACOS
#include <algorithm>
#include <iterator>

#include <QFontDatabase>
#include <QGuiApplication>
#include <qpa/qplatformfontdatabase.h>

#include <CoreText/CoreText.h>

namespace {
    const QString uiFamily = QStringLiteral("Biscuit UI");

    // The installed face with this PostScript name, resolved (file and style
    // name, which the FreeType engine needs), retained; null if missing.
    CTFontDescriptorRef installedFace(const char *postScriptName) {
        CFStringRef name = CFStringCreateWithCString(nullptr, postScriptName, kCFStringEncodingUTF8);
        CTFontDescriptorRef plain = CTFontDescriptorCreateWithNameAndSize(name, 0);
        CFRelease(name);
        CTFontDescriptorRef matched = CTFontDescriptorCreateMatchingFontDescriptor(plain, nullptr);
        CFRelease(plain);
        if (!matched) {
            return nullptr;
        }
        // Matching falls back to another font for an unknown name.
        auto found = static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(matched, kCTFontNameAttribute));
        const bool same = found && QString::fromCFString(found) == QLatin1String(postScriptName);
        if (found) {
            CFRelease(found);
        }
        if (!same) {
            CFRelease(matched);
            return nullptr;
        }
        return matched;
    }

    bool registerFaces() {
        struct Face {
            const char *postScriptName;
            const char *styleName;
            QFont::Weight weight;
            QFont::Style style;
        };
        const Face faces[] = {
            {"HelveticaNeue", "Regular", QFont::Normal, QFont::StyleNormal},
            {"HelveticaNeue-Italic", "Italic", QFont::Normal, QFont::StyleItalic},
            {"HelveticaNeue-Medium", "Bold", QFont::Bold, QFont::StyleNormal},
            {"HelveticaNeue-MediumItalic", "Bold Italic", QFont::Bold, QFont::StyleItalic},
        };
        CTFontDescriptorRef found[std::size(faces)] = {};
        for (size_t i = 0; i < std::size(faces); ++i) {
            found[i] = installedFace(faces[i].postScriptName);
        }
        // All or nothing: a family without bold would make it up.
        if (std::find(std::begin(found), std::end(found), nullptr) != std::end(found)) {
            for (CTFontDescriptorRef face : found) {
                if (face) CFRelease(face);
            }
            return false;
        }
        QFontDatabase::families();   // the system fonts first, then this family
        QSupportedWritingSystems writingSystems;
        for (QFontDatabase::WritingSystem system : QFontDatabase::writingSystems("Helvetica Neue")) {
            writingSystems.setSupported(system);
        }
        // The font database owns the descriptors from here (released with it).
        for (size_t i = 0; i < std::size(faces); ++i) {
            QPlatformFontDatabase::registerFont(uiFamily, faces[i].styleName, QString(), faces[i].weight, faces[i].style,
                                                QFont::Unstretched, true, true, 0, false, false, writingSystems,
                                                const_cast<__CTFontDescriptor *>(found[i]));
        }
        return true;
    }
}

QString MacFonts::registerUiFamily() {
    if (!registerFaces()) {
        return {};
    }
    // Qt builds the font database again when fonts are added or removed.
    static const bool watching = [] {
        QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp, [] { registerFaces(); });
        return true;
    }();
    Q_UNUSED(watching);
    return uiFamily;
}

#else

QString MacFonts::registerUiFamily() {
    return {};
}

#endif
