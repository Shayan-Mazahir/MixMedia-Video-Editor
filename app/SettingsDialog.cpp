// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppPaths.h"
#include "SettingsDialog.h"
#include "AppSettings.h"
#include "HelpWindow.h"
#include "NumberSlider.h"
#include "Theme.h"
#include "UpdateChecker.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumWidth(480);

    // ---- Performance ----
    auto* performance = new QWidget;
    auto* layout = new QVBoxLayout(performance);
    auto* intro = new QLabel("MixMedia normally works out by itself how much of your computer to use. "
                             "Want a say? Tick the box. Handy for keeping some of the computer free "
                             "while it exports, or on a laptop running on battery.");
    intro->setWordWrap(true);
    intro->setProperty("role", "dim");
    layout->addWidget(intro);
    m_advanced = new QCheckBox("Let me choose (advanced)");
    layout->addWidget(m_advanced);

    m_details = new QGroupBox("How much to use");
    auto* form = new QFormLayout(m_details);
    const int cores = ve_cpu_threads();
    m_threads = new NumberSlider(1, cores, 1, cores, 0, cores);
    form->addRow("CPU threads", m_threads);
    m_threadsNote = new QLabel;
    m_threadsNote->setProperty("role", "hint");
    m_threadsNote->setWordWrap(true);
    form->addRow(m_threadsNote);
    m_gpuExport = new QCheckBox("Use the graphics card for exporting");
    m_gpuCaptions = new QCheckBox("Use the graphics card for auto-captions");
    form->addRow(m_gpuExport);
    form->addRow(m_gpuCaptions);
    layout->addWidget(m_details);
    layout->addStretch();

    QSettings s = AppPaths::settings();
    m_advanced->setChecked(AppSettings::advancedPerformance());
    m_threads->setValue(AppSettings::threads());
    m_gpuExport->setChecked(s.value("performance/gpuExport", true).toBool());
    m_gpuCaptions->setChecked(s.value("performance/gpuCaptions", true).toBool());
    connect(m_advanced, &QCheckBox::toggled, this, &SettingsDialog::refresh);
    connect(m_threads, &NumberSlider::valueChanged, this, &SettingsDialog::refresh);
    refresh();

    // ---- Updates ----
    auto* updates = new QWidget;
    auto* updatesLayout = new QVBoxLayout(updates);
    auto* updatesIntro = new QLabel("MixMedia can tell you when there's a new version. It checks when you open it, "
                                    "at most once a day, and only asks GitHub which versions exist.");
    updatesIntro->setWordWrap(true);
    updatesIntro->setProperty("role", "dim");
    m_updatesReleases = new QRadioButton("New releases");
    m_updatesPre = new QRadioButton("Releases and pre-releases (test versions)");
    m_updatesOff = new QRadioButton("Don't check");
    using C = UpdateChecker::Channel;
    (UpdateChecker::channel() == C::Off ? m_updatesOff : UpdateChecker::channel() == C::PreReleases ? m_updatesPre : m_updatesReleases)
        ->setChecked(true);
    auto* checkNow = new QPushButton("Check now");
    connect(checkNow, &QPushButton::clicked, this, [this] { UpdateChecker::checkNow(this); });
    updatesLayout->addWidget(updatesIntro);
    for (QWidget* w : { static_cast<QWidget*>(m_updatesReleases), static_cast<QWidget*>(m_updatesPre), static_cast<QWidget*>(m_updatesOff) })
        updatesLayout->addWidget(w);
    auto* checkRow = new QHBoxLayout;
    checkRow->addWidget(checkNow);
    checkRow->addStretch();
    updatesLayout->addLayout(checkRow);
    updatesLayout->addStretch();

    // ---- Appearance ----
    auto* appearance = new QWidget;
    auto* appearanceLayout = new QVBoxLayout(appearance);
    auto* themeLabel = new QLabel("Theme");
    themeLabel->setProperty("role", "heading");
    m_themeDark = new QRadioButton("Dark");
    m_themeLight = new QRadioButton("Light");
    m_themeSystem = new QRadioButton("Match the system (switches when your computer does)");
    (Theme::savedMode() == Theme::Mode::Light ? m_themeLight : Theme::savedMode() == Theme::Mode::System ? m_themeSystem : m_themeDark)
        ->setChecked(true);
    appearanceLayout->addWidget(themeLabel);
    for (QRadioButton* r : { m_themeDark, m_themeLight, m_themeSystem }) {
        appearanceLayout->addWidget(r);
        // (try it straight away; Cancel puts it back)
        connect(r, &QRadioButton::toggled, this, [this](bool on) {
            if (on)
                Theme::apply(*qApp, m_themeLight->isChecked() ? Theme::Mode::Light
                                    : m_themeSystem->isChecked() ? Theme::Mode::System : Theme::Mode::Dark);
        });
    }
    appearanceLayout->addStretch();

    // ---- Files: where everything's kept ----
    auto* files = new QWidget;
    auto* filesLayout = new QVBoxLayout(files);
    auto* where = new QLabel(AppPaths::portable()
                                 ? "Everything MixMedia keeps is in one folder, right next to the program:"
                                 : "MixMedia's own folder couldn't be written to (it's installed for everyone), "
                                   "so everything it keeps is in this folder instead:");
    where->setWordWrap(true);
    auto* path = new QLabel(QDir::toNativeSeparators(AppPaths::data()));
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setFont(Theme::font(1.0, true));
    auto* inside = new QLabel("<b>models</b>: speech models for auto-captions (drop your own in here)<br>"
                              "<b>fonts</b>: extra fonts for titles and subtitles (.ttf, .otf), "
                              "ready next time MixMedia starts<br>"
                              "<b>settings.ini</b>: these settings<br>"
                              "<b>autosave</b>: backups, in case it crashes<br>"
                              "<b>cache</b>: pictures it can make again (fine to delete)");
    inside->setWordWrap(true);
    inside->setProperty("role", "dim");
    auto* open = new QPushButton("Open the folder");
    connect(open, &QPushButton::clicked, this, [this] { AppPaths::openFolder(this, AppPaths::data()); });
    auto* reset = new QLabel(AppPaths::portable() ? "Uninstalling is just deleting the MixMedia folder: this goes with it."
                                                  : "Delete this folder to reset MixMedia completely.");
    reset->setWordWrap(true);
    reset->setProperty("role", "hint");
    filesLayout->addWidget(where);
    filesLayout->addWidget(path);
    filesLayout->addWidget(inside);
    filesLayout->addWidget(open, 0, Qt::AlignLeft);
    filesLayout->addWidget(reset);
    filesLayout->addStretch();

    auto* tabs = new QTabWidget;
    tabs->addTab(appearance, "Appearance");
    tabs->addTab(performance, "Performance");
    tabs->addTab(updates, "Updates");
    tabs->addTab(files, "Files");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    connect(buttons, &QDialogButtonBox::helpRequested, [] { HelpWindow::open("settings.md"); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        Theme::apply(*qApp, Theme::savedMode()); // (undo any theme you were trying out)
        reject();
    });
    auto* outer = new QVBoxLayout(this);
    outer->addWidget(tabs);
    outer->addWidget(buttons);
    Theme::fit(*this);
}

void SettingsDialog::refresh()
{
    m_details->setEnabled(m_advanced->isChecked());
    const int cores = ve_cpu_threads();
    int n = int(m_threads->value());
    m_threadsNote->setText(QString("%1 of %2 (%3%). Fewer = the rest of the computer stays snappier, "
                                   "but exporting and captions take longer.")
                               .arg(n).arg(cores).arg(n * 100 / cores));
}

void SettingsDialog::save()
{
    QSettings s = AppPaths::settings();
    s.setValue("performance/advanced", m_advanced->isChecked());
    s.setValue("performance/threads", int(m_threads->value()));
    s.setValue("performance/gpuExport", m_gpuExport->isChecked());
    s.setValue("performance/gpuCaptions", m_gpuCaptions->isChecked());
    AppSettings::apply();
    Theme::saveMode(m_themeLight->isChecked() ? Theme::Mode::Light : m_themeSystem->isChecked() ? Theme::Mode::System : Theme::Mode::Dark);
    UpdateChecker::setChannel(m_updatesOff->isChecked()   ? UpdateChecker::Channel::Off
                              : m_updatesPre->isChecked() ? UpdateChecker::Channel::PreReleases
                                                          : UpdateChecker::Channel::Releases);
}
