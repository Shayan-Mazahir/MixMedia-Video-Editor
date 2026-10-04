// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVersionNumber>

#include <optional>

class QWidget;

// Asks GitHub whether there's a newer MixMedia. Only ever when the app's just opened (at most
// once a day) or when you ask, and never if you've said not to.
namespace UpdateChecker {

enum class Channel { Off, Releases, PreReleases };
Channel channel();           // what you picked
bool channelChosen();        // asked yet?
void setChannel(Channel c);

struct Release {
    QString tag;              // e.g. "v0.5.1"
    QVersionNumber version;
    bool preRelease = false;
    bool draft = false;
    QDateTime published;
    QString url;              // the release's page
    QString notes;            // its description (Markdown)
};

// GitHub's list of releases (the JSON from /repos/.../releases)
QList<Release> parseReleases(const QByteArray& json);

// The newest release worth mentioning, if any. Automatic checks wait until a release is an
// hour old (so its downloads are up) and leave out skipped versions; asking by hand doesn't.
std::optional<Release> pickUpdate(const QList<Release>& releases, const QVersionNumber& current, Channel channel,
                                  const QDateTime& now, const QString& skipped, bool byHand);

// Opened fresh: asks which updates you'd like (the first time), then checks if it's been a day
void checkAtStartup(QWidget* parent);
// Help → Check for updates
void checkNow(QWidget* parent);

} // namespace UpdateChecker
