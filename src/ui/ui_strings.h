#pragma once

#include <QCoreApplication>
#include <QString>

#include <string>

namespace sa::ui {

// Translation context for every user-visible string. Chinese is the source
// language; resources/i18n/speakanything_en.ts carries the English text.
struct Ui {
    Q_DECLARE_TR_FUNCTIONS(SpeakAnything)
};

inline QString to_qstring(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

inline std::string to_utf8_string(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

} // namespace sa::ui
