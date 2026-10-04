// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "UpdateChecker.h"

#include "Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace UpdateChecker {

namespace {

const char* ReleasesApi = "https://api.github.com/repos/Shayan-Mazahir/MixMedia-Video-Editor/releases?per_page=30";
constexpr qint64 HourInSeconds = 60 * 60;
constexpr qint64 DayInSeconds = 24 * HourInSeconds;

QVersionNumber currentVersion()
{
    return QVersionNumber::fromString(QStringLiteral(MIXMEDIA_VERSION));
}

// Asks GitHub, waiting for the answer (with a short timeout, so a bad connection can't hang us)
bool fetch(QByteArray* json, QString* error)
{
    QNetworkAccessManager network;
    QNetworkRequest request { QUrl(ReleasesApi) };
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "MixMedia/" MIXMEDIA_VERSION);
    request.setTransferTimeout(15000);
    QNetworkReply* reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *json = reply->readAll();
    *error = reply->errorString();
    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

void announce(QWidget* parent, const Release& r, bool byHand)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Update available");
    dialog.setMinimumSize(520, 420);
    const bool fresh = r.published.secsTo(QDateTime::currentDateTimeUtc()) < HourInSeconds;
    auto* heading = new QLabel(QString("<b>MixMedia %1</b> is out%2 (you've got %3).%4")
                                   .arg(r.tag, r.preRelease ? " as a pre-release" : "", QStringLiteral(MIXMEDIA_VERSION),
                                        fresh ? "<br><i>It's brand new: the downloads might still be getting ready.</i>" : ""));
    heading->setWordWrap(true);
    auto* notes = new QTextBrowser;
    notes->setMarkdown(r.notes.isEmpty() ? QStringLiteral("(No notes for this one.)") : r.notes);
    notes->setOpenExternalLinks(true);
    auto* buttons = new QDialogButtonBox;
    QPushButton* download = buttons->addButton("Download", QDialogButtonBox::AcceptRole);
    QPushButton* skip = buttons->addButton("Skip this version", QDialogButtonBox::DestructiveRole);
    buttons->addButton("Later", QDialogButtonBox::RejectRole);
    download->setDefault(true);
    QObject::connect(buttons, &QDialogButtonBox::clicked, &dialog, [&](QAbstractButton* b) {
        if (b == download)
            QDesktopServices::openUrl(QUrl(r.url));
        else if (b == skip)
            QSettings().setValue("updates/skipped", r.tag);
        dialog.accept();
    });
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(heading);
    layout->addWidget(new QLabel("What's new:"));
    layout->addWidget(notes, 1);
    layout->addWidget(buttons);
    skip->setVisible(!byHand || QSettings().value("updates/skipped").toString() != r.tag);
    Theme::fit(dialog);
    dialog.exec();
}

// Which updates do you want? Shown the first time (and from Settings)
bool askForChannel(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Updates");
    dialog.setMinimumWidth(480);
    auto* intro = new QLabel("Want MixMedia to tell you when there's a new version? It checks when you open it "
                             "(at most once a day), and only asks GitHub which versions exist: nothing about you "
                             "or your videos gets sent.");
    intro->setWordWrap(true);
    auto* releases = new QRadioButton("Yes, new releases");
    auto* pre = new QRadioButton("Yes, releases and pre-releases (test versions, might be a bit rough)");
    auto* off = new QRadioButton("No, don't check");
    releases->setChecked(true);
    auto* later = new QLabel("You can change this any time in Edit → Settings → Updates.");
    later->setProperty("role", "hint");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(intro);
    for (QWidget* w : { static_cast<QWidget*>(releases), static_cast<QWidget*>(pre), static_cast<QWidget*>(off), static_cast<QWidget*>(later) })
        layout->addWidget(w);
    layout->addWidget(buttons);
    Theme::fit(dialog);
    dialog.exec(); // (closing it counts as the default: releases)
    setChannel(off->isChecked() ? Channel::Off : pre->isChecked() ? Channel::PreReleases : Channel::Releases);
    return channel() != Channel::Off;
}

} // namespace

Channel channel()
{
    QString c = QSettings().value("updates/channel").toString();
    return c == "pre" ? Channel::PreReleases : c == "off" ? Channel::Off : Channel::Releases;
}

bool channelChosen()
{
    return QSettings().contains("updates/channel");
}

void setChannel(Channel c)
{
    QSettings().setValue("updates/channel", c == Channel::Off ? "off" : c == Channel::PreReleases ? "pre" : "releases");
}

QList<Release> parseReleases(const QByteArray& json)
{
    QList<Release> out;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array()) {
        QJsonObject o = v.toObject();
        Release r;
        r.tag = o["tag_name"].toString();
        QString number = r.tag.startsWith('v') || r.tag.startsWith('V') ? r.tag.mid(1) : r.tag;
        r.version = QVersionNumber::fromString(number);
        r.preRelease = o["prerelease"].toBool();
        r.draft = o["draft"].toBool();
        r.published = QDateTime::fromString(o["published_at"].toString(), Qt::ISODate);
        r.url = o["html_url"].toString();
        r.notes = o["body"].toString();
        if (!r.version.isNull())
            out << r;
    }
    return out;
}

std::optional<Release> pickUpdate(const QList<Release>& releases, const QVersionNumber& current, Channel channel,
                                  const QDateTime& now, const QString& skipped, bool byHand)
{
    if (channel == Channel::Off && !byHand)
        return std::nullopt;
    std::optional<Release> best;
    for (const Release& r : releases) {
        if (r.draft || !r.published.isValid() || r.version <= current)
            continue;
        if (r.preRelease && channel != Channel::PreReleases)
            continue;
        if (!byHand && (r.published.secsTo(now) < HourInSeconds || r.tag == skipped))
            continue; // (too fresh: its downloads might not be up yet)
        if (!best || r.version > best->version)
            best = r;
    }
    return best;
}

void checkAtStartup(QWidget* parent)
{
    if (!channelChosen() && !askForChannel(parent))
        return;
    if (channel() == Channel::Off)
        return;
    QSettings s;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime last = s.value("updates/lastCheck").toDateTime();
    if (last.isValid() && last.secsTo(now) < DayInSeconds)
        return; // checked within the last day
    QByteArray json;
    QString error;
    if (!fetch(&json, &error))
        return; // offline or GitHub's busy: quietly try again next time
    s.setValue("updates/lastCheck", now);
    if (auto r = pickUpdate(parseReleases(json), currentVersion(), channel(), now, s.value("updates/skipped").toString(), false))
        announce(parent, *r, false);
}

void checkNow(QWidget* parent)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QByteArray json;
    QString error;
    bool ok = fetch(&json, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(parent, "Check for updates", "Couldn't reach GitHub:\n" + error);
        return;
    }
    QSettings().setValue("updates/lastCheck", QDateTime::currentDateTimeUtc());
    // By hand you get told about anything newer, whatever the setting (pre-releases only if you want them)
    Channel c = channel() == Channel::Off ? Channel::Releases : channel();
    if (auto r = pickUpdate(parseReleases(json), currentVersion(), c, QDateTime::currentDateTimeUtc(), {}, true))
        announce(parent, *r, true);
    else
        QMessageBox::information(parent, "Check for updates",
                                 QString("You've got the newest version (%1).").arg(QStringLiteral(MIXMEDIA_VERSION)));
}

} // namespace UpdateChecker
