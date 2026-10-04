// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppSettings.h"
#include "MainWindow.h"
#include "Theme.h"
#include "UpdateChecker.h"

#include <QApplication>
#include <QStyleHints>
#include <QTimer>
#include <QIcon>
#include <QStandardPaths>
#include <QPalette>
#include <QStyleFactory>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("MixMedia Video Editor");
    QApplication::setOrganizationName("MixMedia"); // where settings and auto-saves live
    QApplication::setWindowIcon(QIcon(":/mixmedia.png"));
    // Lets Linux desktops match the window to its icon, but only once it's installed
    // (otherwise the desktop complains it can't find mixmedia.desktop)
    if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, "mixmedia.desktop").isEmpty())
        QGuiApplication::setDesktopFileName("mixmedia");
    // (MIXMEDIA_THEME=light/dark/system tries one without changing the setting, handy for testing)
    const QString forced = qEnvironmentVariable("MIXMEDIA_THEME");
    Theme::apply(app, forced == "light" ? Theme::Mode::Light : forced == "dark" ? Theme::Mode::Dark
                      : forced == "system" ? Theme::Mode::System : Theme::savedMode());
    // "Match the system": follow the desktop if it switches between light and dark
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] {
        if (Theme::savedMode() == Theme::Mode::System)
            Theme::apply(app, Theme::Mode::System);
    });
    AppSettings::apply(); // how much of the computer we're allowed to use

    // mixmedia [files...]            opens a project, or imports media
    // mixmedia --demo out.png files  puts them on the timeline, screenshots, quits
    QStringList args = app.arguments().mid(1);
    QString demoShot;
    if (args.size() >= 2 && args.first() == "--demo") {
        demoShot = args.at(1);
        args = args.mid(2);
    }

    MainWindow window;
    if (demoShot.isEmpty())
        window.restoreLayout(); // (as you left it, or a size that suits this screen)
    else
        window.resize(1400, 850); // (demo screenshots are always the same size)
    window.show();

    if (!demoShot.isEmpty())
        window.runDemo(args, demoShot);
    else {
        bool recovered = window.offerRecovery();
        window.openFiles(args);
        // Once the window's up, one thing at a time: the welcome screen (if nothing's open yet),
        // then any updates (which asks which you'd like, the very first time)
        const bool welcome = !recovered && args.isEmpty();
        QTimer::singleShot(300, &window, [&window, welcome] {
            if (welcome)
                window.showWelcome();
            UpdateChecker::checkAtStartup(&window);
        });
    }

    return app.exec();
}
