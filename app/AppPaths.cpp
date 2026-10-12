// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppPaths.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#endif

namespace AppPaths {

namespace {

// The folder you'd think of as "where MixMedia is"
QString appFolder()
{
    // The AppImage runs from a temporary read-only copy; the real file is wherever you put it
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty())
        return QFileInfo(appImage).absolutePath();
    QDir dir(QCoreApplication::applicationDirPath());
    // The Linux folder download keeps the program in MixMedia/usr/bin, but you start it from MixMedia/
    if (dir.path().endsWith("/usr/bin") && QFileInfo::exists(dir.filePath("../../AppRun")))
        return QDir::cleanPath(dir.filePath("../.."));
    return dir.path();
}

bool canWrite(const QString& folder)
{
    if (!QDir().mkpath(folder))
        return false;
    // (actually trying is the only reliable way to tell, especially on Windows)
    QFile probe(QDir(folder).filePath(".can-write"));
    const bool ok = probe.open(QIODevice::WriteOnly);
    probe.close();
    probe.remove();
    return ok;
}

QString subfolder(const QString& name)
{
    const QString path = QDir(data()).filePath(name);
    QDir().mkpath(path);
    return path;
}

// Moves the files from an old folder to the new one, then removes the old one if it's empty
void moveFiles(const QString& from, const QString& to)
{
    QDir old(from);
    if (!old.exists() || QDir(from) == QDir(to))
        return;
    QDir().mkpath(to);
    for (const QFileInfo& f : old.entryInfoList(QDir::Files)) {
        const QString target = QDir(to).filePath(f.fileName());
        if (QFileInfo::exists(target))
            continue;
        // (a rename is instant; between drives it has to be copied)
        if (!QFile::rename(f.absoluteFilePath(), target) && QFile::copy(f.absoluteFilePath(), target))
            QFile::remove(f.absoluteFilePath());
    }
    QDir().rmdir(old.absolutePath());
}

// Earlier versions kept things in the usual hidden places (and the registry on Windows).
// Bring them over the first time, and tidy up behind.
void bringOver()
{
    const QString file = QDir(data()).filePath("settings.ini");
    if (QFileInfo::exists(file))
        return; // (done already, or never needed)

    QSettings old(QSettings::NativeFormat, QSettings::UserScope, QCoreApplication::organizationName(),
                  QCoreApplication::applicationName());
    const QStringList keys = old.allKeys();
    {
        QSettings now = settings();
        for (const QString& key : keys)
            now.setValue(key, old.value(key));
        now.setValue("app/version", MIXMEDIA_VERSION); // (so the file's there even if empty)
    }
    if (!keys.isEmpty()) {
        old.clear();
        old.sync();
        const QFileInfo oldFile(old.fileName()); // (on Windows that's the registry, not a file)
        if (oldFile.isFile()) {
            QFile::remove(oldFile.absoluteFilePath());
            QDir().rmdir(oldFile.absolutePath());
        }
    }

    const QString oldData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!oldData.isEmpty() && QDir(oldData) != QDir(data())) {
        moveFiles(QDir(oldData).filePath("models"), models());
        moveFiles(QDir(oldData).filePath("autosave"), autosave());
        QDir().rmdir(oldData);
        QDir().rmdir(QFileInfo(oldData).absolutePath()); // (the "MixMedia" folder above it, if empty)
    }
}

// The old cache: only pictures MixMedia made (and makes again), so it just goes
void tidyOldCache()
{
    const QString oldCache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (oldCache.isEmpty() || QDir(oldCache) == QDir(cache()) || !QDir(oldCache).exists())
        return;
    for (const char* made : { "titles", "theme", "theme-dark", "theme-light" })
        QDir(QDir(oldCache).filePath(made)).removeRecursively();
    QDir().rmdir(oldCache);
    QDir().rmdir(QFileInfo(oldCache).absolutePath());
}

} // namespace

QString data()
{
    // (the tests keep well away from everything real)
    if (QStandardPaths::isTestModeEnabled())
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    static const QString folder = [] {
        const QString beside = QDir(appFolder()).filePath("MixMedia data");
        return canWrite(beside) ? beside : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }();
    return folder;
}

bool portable()
{
    return QDir(data()) == QDir(QDir(appFolder()).filePath("MixMedia data"));
}

QString models() { return subfolder("models"); }
QString fonts() { return subfolder("fonts"); }
QString autosave() { return subfolder("autosave"); }
QString cache() { return subfolder("cache"); }

QSettings settings()
{
    return QSettings(QDir(data()).filePath("settings.ini"), QSettings::IniFormat);
}

void setUp()
{
    bringOver();
    tidyOldCache();
    for (const QString& folder : { models(), fonts(), autosave(), cache() })
        QDir().mkpath(folder);

    // A note for anyone poking around in there
    const QString note = QDir(data()).filePath("What's in here.txt");
    if (!QFileInfo::exists(note)) {
        QFile f(note);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write("Everything MixMedia keeps is in this folder:\n"
                    "\n"
                    "  settings.ini   your settings\n"
                    "  models         speech models for auto-captions. Drop your own whisper.cpp models\n"
                    "                 (ggml-....bin) in here and they show up in the list.\n"
                    "  fonts          extra fonts for titles and subtitles. Drop .ttf or .otf files in\n"
                    "                 here and they show up next time MixMedia starts.\n"
                    "  autosave       backups, in case it crashes\n"
                    "  cache          pictures MixMedia can make again (fine to delete)\n"
                    "\n"
                    "Delete this folder to reset MixMedia completely.\n");
    }

    // Your own fonts
    for (const QFileInfo& f : QDir(fonts()).entryInfoList({ "*.ttf", "*.otf", "*.ttc", "*.TTF", "*.OTF" }, QDir::Files))
        QFontDatabase::addApplicationFont(f.absoluteFilePath());
}

void openFolder(QWidget* parent, const QString& folder)
{
    QDir().mkpath(folder);
#ifdef Q_OS_LINUX
    // Ask the desktop's file manager over D-Bus first: works even where there's no xdg-open
    // (like inside a toolbox container)
    QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                                       "org.freedesktop.FileManager1", "ShowFolders");
    call << QStringList { QUrl::fromLocalFile(folder).toString() } << QString();
    if (QDBusConnection::sessionBus().call(call, QDBus::Block, 5000).type() != QDBusMessage::ErrorMessage)
        return;
#endif
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        return;
    // No file manager to open it with: at least say where it is
    QMessageBox box(QMessageBox::Information, "Here's the folder",
                    "Couldn't open a file manager here, but this is where it is:\n\n" + QDir::toNativeSeparators(folder),
                    QMessageBox::Ok, parent);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton* copy = box.addButton("Copy the location", QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == copy)
        QApplication::clipboard()->setText(QDir::toNativeSeparators(folder));
}

} // namespace AppPaths
