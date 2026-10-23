// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppPaths.h"
#include "UpdateChecker.h"
#include "SelfUpdate.h"

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
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QSet>
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

// The release notes, pictures and all: pictures on the web get downloaded as they're needed
// (a plain text browser only shows pictures it already has, and a broken icon otherwise)
class NotesView : public QTextBrowser {
public:
    void show(const QString& markdown)
    {
        m_markdown = markdown;
        setMarkdown(markdown);
    }

protected:
    QVariant loadResource(int type, const QUrl& url) override
    {
        if (type != QTextDocument::ImageResource || (url.scheme() != "https" && url.scheme() != "http"))
            return QTextBrowser::loadResource(type, url);
        if (m_pictures.contains(url))
            return m_pictures.value(url);
        if (!m_asked.contains(url)) {
            m_asked << url;
            QNetworkRequest request(url);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setRawHeader("User-Agent", "MixMedia/" MIXMEDIA_VERSION);
            request.setTransferTimeout(15000);
            QNetworkReply* reply = m_network.get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
                QImage picture;
                if (reply->error() == QNetworkReply::NoError && picture.loadFromData(reply->readAll())) {
                    // Never wider than the notes
                    const int room = viewport()->width() - 24;
                    if (room > 50 && picture.width() > room)
                        picture = picture.scaledToWidth(room, Qt::SmoothTransformation);
                    m_pictures.insert(url, picture);
                    setMarkdown(m_markdown); // (lays it out again, with the picture in)
                }
                reply->deleteLater();
            });
        }
        // While it's coming (or if it can't): an empty space rather than a broken-picture icon
        QImage blank(1, 1, QImage::Format_ARGB32);
        blank.fill(Qt::transparent);
        return blank;
    }

private:
    QString m_markdown;
    QNetworkAccessManager m_network;
    QHash<QUrl, QImage> m_pictures;
    QSet<QUrl> m_asked;
};

// "25 minutes ago", "about 3 hours ago", "2 days ago"
QString ago(qint64 seconds)
{
    if (seconds < 90)
        return "just now";
    if (seconds < 3600)
        return QString("%1 minutes ago").arg(seconds / 60);
    if (seconds < 2 * 3600)
        return "about an hour ago";
    if (seconds < 48 * 3600)
        return QString("about %1 hours ago").arg(seconds / 3600);
    return QString("%1 days ago").arg(seconds / 86400);
}

void announce(QWidget* parent, const Release& r, bool byHand)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Update available");
    dialog.setMinimumSize(520, 420);
    const qint64 age = r.published.secsTo(QDateTime::currentDateTimeUtc());
    const bool fresh = age < HourInSeconds;
    const QString when = QLocale().toString(r.published.toLocalTime(), QLocale::ShortFormat);
    auto* heading = new QLabel(QString("<b>MixMedia %1</b> is out%2 (you've got %3).<br>"
                                       "<span style='color:%4;'>Released %5 (%6).</span>%7")
                                   .arg(r.tag, r.preRelease ? " as a pre-release" : "", QStringLiteral(MIXMEDIA_VERSION),
                                        Theme::colours().dim.name(), when, ago(age),
                                        fresh ? "<br><i>It's brand new: the downloads might still be getting ready.</i>" : ""));
    heading->setWordWrap(true);
    auto* notes = new NotesView;
    notes->setOpenExternalLinks(true);
    notes->show(r.notes.isEmpty() ? QStringLiteral("(No notes for this one.)") : r.notes);
    // This copy can update itself (and the release has the file it needs)? Then offer that,
    // as well as downloading it yourself
    const SelfUpdate::Kind how = SelfUpdate::kind();
    const std::optional<Asset> file = r.asset(SelfUpdate::assetName(how));
    const bool canUpdate = how != SelfUpdate::Kind::None && file.has_value();
    auto* buttons = new QDialogButtonBox;
    QPushButton* update = canUpdate ? buttons->addButton("Update now", QDialogButtonBox::AcceptRole) : nullptr;
    QPushButton* download = buttons->addButton(canUpdate ? "Download it myself" : "Download",
                                               canUpdate ? QDialogButtonBox::ActionRole : QDialogButtonBox::AcceptRole);
    QPushButton* skip = buttons->addButton("Skip this version", QDialogButtonBox::DestructiveRole);
    buttons->addButton("Later", QDialogButtonBox::RejectRole);
    (update ? update : download)->setDefault(true);
    if (update)
        update->setToolTip(QString("Downloads it (%1 MB), checks it, swaps it in and restarts. Your settings, "
                                   "models and projects stay as they are.").arg(file->size / (1024 * 1024)));
    bool updating = false;
    QObject::connect(buttons, &QDialogButtonBox::clicked, &dialog, [&](QAbstractButton* b) {
        if (b == update)
            updating = true;
        else if (b == download)
            QDesktopServices::openUrl(QUrl(r.url));
        else if (b == skip)
            AppPaths::settings().setValue("updates/skipped", r.tag);
        dialog.accept();
    });
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(heading);
    layout->addWidget(new QLabel("What's new:"));
    layout->addWidget(notes, 1);
    layout->addWidget(buttons);
    skip->setVisible(!byHand || AppPaths::settings().value("updates/skipped").toString() != r.tag);
    Theme::fit(dialog);
    dialog.exec();
    if (!updating)
        return;

    // If it doesn't work: fine if it's brand new (the downloads take a while to appear), but
    // after five hours something's probably gone wrong on my end
    const QString ifItFails = age < 5 * HourInSeconds
        ? QString("It only came out %1, so the downloads might still be getting ready. Try again in a bit.").arg(ago(age))
        : QString("It came out %1. If it's been more than 5 hours and it still doesn't work, I probably messed up "
                  "somewhere... sooo ugh... I'll get back to it eventually... hopefully... %2\n\n"
                  "Meanwhile, \"Download it myself\" gets it from the release page.")
              .arg(ago(age), Theme::emoji("😅", ":/"));
    const QString program = SelfUpdate::install(parent, *file, ifItFails);
    if (program.isEmpty())
        return;
    QMessageBox done(QMessageBox::Information, "MixMedia's updated",
                     QString("MixMedia %1 is ready. Restart now to start using it?").arg(r.tag), QMessageBox::NoButton, parent);
    QPushButton* now = done.addButton("Restart now", QMessageBox::AcceptRole);
    done.addButton("Later", QMessageBox::RejectRole);
    done.setDefaultButton(now);
    done.exec();
    if (done.clickedButton() != now || !SelfUpdate::restartInto(program))
        QMessageBox::information(parent, "MixMedia's updated", "No rush: it'll be the new version next time you open MixMedia.");
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
    QString c = AppPaths::settings().value("updates/channel").toString();
    return c == "pre" ? Channel::PreReleases : c == "off" ? Channel::Off : Channel::Releases;
}

bool channelChosen()
{
    return AppPaths::settings().contains("updates/channel");
}

void setChannel(Channel c)
{
    AppPaths::settings().setValue("updates/channel", c == Channel::Off ? "off" : c == Channel::PreReleases ? "pre" : "releases");
}

std::optional<Asset> Release::asset(const QString& name) const
{
    for (const Asset& a : assets)
        if (a.name == name)
            return a;
    return std::nullopt;
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
        for (const QJsonValue& a : o["assets"].toArray()) {
            const QJsonObject file = a.toObject();
            const QString digest = file["digest"].toString();
            r.assets << Asset { file["name"].toString(), file["browser_download_url"].toString(),
                                qint64(file["size"].toDouble()),
                                digest.startsWith("sha256:") ? digest.mid(7).toLower() : QString() };
        }
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
    QSettings s = AppPaths::settings();
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
    AppPaths::settings().setValue("updates/lastCheck", QDateTime::currentDateTimeUtc());
    // By hand you get told about anything newer, whatever the setting (pre-releases only if you want them)
    Channel c = channel() == Channel::Off ? Channel::Releases : channel();
    if (auto r = pickUpdate(parseReleases(json), currentVersion(), c, QDateTime::currentDateTimeUtc(), {}, true))
        announce(parent, *r, true);
    else
        QMessageBox::information(parent, "Check for updates",
                                 QString("You've got the newest version (%1).").arg(QStringLiteral(MIXMEDIA_VERSION)));
}

} // namespace UpdateChecker
