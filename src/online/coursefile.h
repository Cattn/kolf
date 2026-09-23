// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>

namespace Kolf::Online {
constexpr qsizetype MaximumCourseBytes = 4 * 1024 * 1024;
constexpr qsizetype CourseChunkBytes = 48 * 1024;

inline QString rawCourseHash(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

inline QString courseCachePath(const QString &sha256)
{
    static const QRegularExpression hashPattern(QStringLiteral("^[a-f0-9]{64}$"));
    if (!hashPattern.match(sha256).hasMatch()) return {};
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("online/course-cache/%1.kolf").arg(sha256));
}

inline bool validateCourseBytes(const QByteArray &bytes, QString &error)
{
    if (bytes.isEmpty() || bytes.size() > MaximumCourseBytes) {
        error = QStringLiteral("Course exceeds the 4 MiB limit."); return false;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(bytes);
    if (decoder.hasError()) { error = QStringLiteral("Course is not valid UTF-8 text."); return false; }
    static const QRegularExpression binary(QStringLiteral("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f\\x7f]"));
    static const QRegularExpression groupPattern(QStringLiteral("^(\\d+)-([a-z]+)@(-?\\d+),(-?\\d+)(?:\\|(\\d+))?$"));
    static const QRegularExpression keyPattern(QStringLiteral("^[A-Za-z0-9_@.\\[\\]-]+$"));
    static const QSet<QString> allowed{QStringLiteral("ball"), QStringLiteral("blackhole"), QStringLiteral("bridge"),
        QStringLiteral("bumper"), QStringLiteral("course"), QStringLiteral("cup"), QStringLiteral("floater"),
        QStringLiteral("hole"), QStringLiteral("puddle"), QStringLiteral("sand"), QStringLiteral("sign"),
        QStringLiteral("slope"), QStringLiteral("wall"), QStringLiteral("windmill")};
    if (binary.match(text).hasMatch()) { error = QStringLiteral("Course contains binary content."); return false; }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QSet<QString> groups;
    QSet<int> balls, cups, holes;
    QString current, name, author;
    for (const auto &line : text.split(QLatin1Char('\n'))) {
        if (line.size() > 4096) { error = QStringLiteral("Course line is too long."); return false; }
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1Char(';'))) continue;
        if (trimmed.startsWith(QLatin1Char('['))) {
            if (!trimmed.endsWith(QLatin1Char(']'))) { error = QStringLiteral("Malformed course group."); return false; }
            const auto group = trimmed.mid(1, trimmed.size() - 2);
            const auto match = groupPattern.match(group);
            bool validHole = false, validX = false, validY = false;
            const int hole = match.captured(1).toInt(&validHole);
            const qint64 x = match.captured(3).toLongLong(&validX);
            const qint64 y = match.captured(4).toLongLong(&validY);
            if (!match.hasMatch() || !allowed.contains(match.captured(2)) || !validHole || hole > 1000
                || !validX || !validY || x < -100000 || x > 100000 || y < -100000 || y > 100000
                || groups.contains(group) || groups.size() >= 4096) {
                error = QStringLiteral("Unsupported or duplicate course object."); return false;
            }
            groups.insert(group); current = group;
            if (match.captured(2) == QLatin1String("ball")) balls.insert(hole);
            if (match.captured(2) == QLatin1String("cup")) cups.insert(hole);
            if (group == QStringLiteral("%1-hole@-50,-50|0").arg(hole)) holes.insert(hole);
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        const auto key = line.left(equals).trimmed();
        if (current.isEmpty() || equals < 1 || !keyPattern.match(key).hasMatch()
            || key.contains(QStringLiteral("plugin"), Qt::CaseInsensitive)
            || key.contains(QStringLiteral("script"), Qt::CaseInsensitive)
            || key.contains(QStringLiteral("exec"), Qt::CaseInsensitive)) {
            error = QStringLiteral("Malformed or unsafe course property."); return false;
        }
        const auto value = line.mid(equals + 1).trimmed();
        if (value.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
            || value.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive)
            || value.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            error = QStringLiteral("Executable or archive content is not allowed."); return false;
        }
        if (current == QLatin1String("0-course@-50,-50") && (key == QLatin1String("Name") || key == QLatin1String("name"))) name = value;
        if (current == QLatin1String("0-course@-50,-50") && key == QLatin1String("author")) author = value;
        if (key == QLatin1String("par") && current.endsWith(QStringLiteral("-hole@-50,-50|0"))) {
            bool validPar = false;
            const int par = value.toInt(&validPar);
            if (!validPar || par < 0 || par > 1000) { error = QStringLiteral("Course par is invalid."); return false; }
        }
    }
    if (name.isEmpty() || name.size() > 64 || author.size() > 128 || holes.isEmpty() || holes.size() > 1000) {
        error = QStringLiteral("Course metadata is incomplete."); return false;
    }
    for (int hole = 1; hole <= holes.size(); ++hole) {
        if (!holes.contains(hole) || !balls.contains(hole) || !cups.contains(hole)) {
            error = QStringLiteral("Course holes are incomplete or out of order."); return false;
        }
    }
    return true;
}
}
