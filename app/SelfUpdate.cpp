// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "SelfUpdate.h"
#include "AppPaths.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>

#include <cstdio>

namespace SelfUpdate {

namespace {

// Swaps one path for another, even if something's already there (and even if it's running:
// Linux keeps the old one going until it closes)
bool swap(const QString& from, const QString& to)
{
    return std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
}

// Downloads `asset` to `file`, checking it's exactly what GitHub says it should be
bool download(QWidget* parent, const UpdateChecker::Asset& asset, const QString& file, QString* error)
{
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = "Couldn't write to " + QFileInfo(file).absolutePath();
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QNetworkAccessManager network;
    QNetworkRequest request { QUrl(asset.url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "MixMedia/" MIXMEDIA_VERSION);
    QNetworkReply* reply = network.get(request);

    QProgressDialog progress(QString("Downloading the update (%1 MB)…").arg(asset.size / (1024 * 1024)), "Cancel", 0, 1000, parent);
    progress.setWindowTitle("Updating MixMedia");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QObject::connect(reply, &QNetworkReply::readyRead, &out, [&] {
        const QByteArray chunk = reply->readAll();
        out.write(chunk);
        hash.addData(chunk);
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress, [&](qint64 got, qint64 total) {
        if (total > 0)
            progress.setValue(int(got * 1000 / total));
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray rest = reply->readAll();
    out.write(rest);
    hash.addData(rest);
    out.close();
    const bool cancelled = progress.wasCanceled();
    progress.reset();
    if (reply->error() != QNetworkReply::NoError) {
        *error = cancelled ? QString() : "The download didn't work: " + reply->errorString();
        reply->deleteLater();
        return false;
    }
    reply->deleteLater();
    // Every byte has to match, or it doesn't go anywhere near the real MixMedia
    if ((asset.size > 0 && out.size() != asset.size)
        || (!asset.sha256.isEmpty() && hash.result().toHex() != asset.sha256.toLatin1())) {
        *error = "The download came out wrong (it doesn't match what GitHub says it should be), "
                 "so nothing was changed. Try again, or download it yourself.";
        return false;
    }
    return true;
}

} // namespace

Kind kind()
{
#ifdef Q_OS_LINUX
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) {
        const QFileInfo file(appImage), folder(file.absolutePath());
        return file.isFile() && file.isWritable() && folder.isWritable() ? Kind::AppImage : Kind::None;
    }
    const QDir folder(AppPaths::appFolder());
    if (QFileInfo::exists(folder.filePath("AppRun")) && QFileInfo::exists(folder.filePath("usr/bin/mixmedia"))
        && QFileInfo(folder.path()).isWritable())
        return Kind::Folder;
#endif
    return Kind::None;
}

QString assetName(Kind kind)
{
    return kind == Kind::AppImage ? QStringLiteral("MixMedia-Linux-x86_64.AppImage")
         : kind == Kind::Folder   ? QStringLiteral("MixMedia-Linux-x86_64.tar.gz")
                                  : QString();
}

bool replaceAppImage(const QString& file, const QString& appImage)
{
    QFile::setPermissions(file, QFile::permissions(appImage) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther
                                    | QFile::ReadOwner | QFile::WriteOwner);
    return swap(file, appImage);
}

bool replaceFolder(const QString& tarball, const QString& folder, QString* error)
{
    const QDir app(folder);
    if (!app.isAbsolute() || !app.exists()) {
        *error = "Couldn't find MixMedia's folder.";
        return false;
    }
    // Unpack next to it (so moving things into place is instant), then swap the pieces over
    const QString fresh = app.filePath(".update-new"), old = app.filePath(".update-old");
    QDir(fresh).removeRecursively();
    QDir(old).removeRecursively();
    QDir().mkpath(fresh);
    QDir().mkpath(old);
    QProcess tar;
    tar.start("tar", { "xzf", tarball, "-C", fresh });
    if (!tar.waitForFinished(5 * 60 * 1000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        *error = "Couldn't unpack the update: " + QString::fromLocal8Bit(tar.readAllStandardError()).trimmed();
        QDir(fresh).removeRecursively();
        QDir(old).removeRecursively();
        return false;
    }
    QDir unpacked(QDir(fresh).filePath("MixMedia"));
    if (!QFileInfo::exists(unpacked.filePath("AppRun")) || !QFileInfo::exists(unpacked.filePath("usr/bin/mixmedia"))) {
        *error = "The update wasn't what was expected (no MixMedia inside), so nothing was changed.";
        QDir(fresh).removeRecursively();
        QDir(old).removeRecursively();
        return false;
    }
    // Everything except your data folder gets swapped. If any of it fails, it all goes back.
    const QStringList pieces = unpacked.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    QStringList movedAside, movedIn;
    bool ok = true;
    for (const QString& name : pieces) {
        if (name == "MixMedia data")
            continue;
        const QString here = app.filePath(name);
        if (QFileInfo(here).exists() || QFileInfo(here).isSymLink()) {
            if (!swap(here, QDir(old).filePath(name))) {
                ok = false;
                break;
            }
            movedAside << name;
        }
        if (!swap(unpacked.filePath(name), here)) {
            ok = false;
            break;
        }
        movedIn << name;
    }
    if (!ok) {
        for (const QString& name : movedIn)
            swap(app.filePath(name), unpacked.filePath(name));
        for (const QString& name : movedAside)
            swap(QDir(old).filePath(name), app.filePath(name));
        *error = "Couldn't put the update in place, so MixMedia was left as it was.";
    }
    QDir(fresh).removeRecursively();
    QDir(old).removeRecursively(); // (the running copy keeps going until it closes)
    return ok;
}

QString install(QWidget* parent, const UpdateChecker::Asset& asset, const QString& ifItFails)
{
    const Kind how = kind();
    if (how == Kind::None)
        return {};
    const QString target = how == Kind::AppImage ? qEnvironmentVariable("APPIMAGE") : AppPaths::appFolder();
    const QString folder = how == Kind::AppImage ? QFileInfo(target).absolutePath() : target;
    // (downloaded next to it, so it's on the same drive and can be swapped in instantly)
    const QString part = QDir(folder).filePath(".mixmedia-update.part");
    QString error;
    bool ok = download(parent, asset, part, &error);
    if (ok) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        ok = how == Kind::AppImage ? replaceAppImage(part, target) : replaceFolder(part, target, &error);
        QApplication::restoreOverrideCursor();
        if (!ok && error.isEmpty())
            error = "Couldn't put the update in place, so MixMedia was left as it was.";
    }
    QFile::remove(part);
    if (!ok) {
        if (!error.isEmpty())
            QMessageBox::warning(parent, "Updating MixMedia", ifItFails.isEmpty() ? error : error + "\n\n" + ifItFails);
        return {};
    }
    return how == Kind::AppImage ? target : QDir(target).filePath("mixmedia");
}

bool restartInto(const QString& program)
{
    // Close the editor the normal way, so it asks about unsaved changes
    for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* main = qobject_cast<QMainWindow*>(w); main && main->isVisible() && !main->close())
            return false;
    // A clean start: none of the old AppImage's settings (they point at a copy that's about to go)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString oldMount = env.value("APPDIR");
    for (const char* key : { "APPIMAGE", "APPDIR", "ARGV0", "OWD" })
        env.remove(key);
    if (!oldMount.isEmpty())
        for (const QString& key : env.keys())
            if (env.value(key).contains(oldMount))
                env.remove(key);
    QProcess next;
    next.setProgram(program);
    next.setProcessEnvironment(env);
    next.setWorkingDirectory(QFileInfo(program).absolutePath());
    next.startDetached();
    qApp->quit();
    return true;
}

} // namespace SelfUpdate
