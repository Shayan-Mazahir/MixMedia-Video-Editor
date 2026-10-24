// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppPaths.h"
#include "Shortcuts.h"
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
#include <QSignalBlocker>
#include <QKeySequenceEdit>
#include <QHeaderView>
#include <QTableWidget>
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
                              "<b>looks</b>: your own colour looks (.cube files) for the Look list<br>"
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
    tabs->addTab(buildShortcuts(), "Shortcuts");
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
    if (m_resetAllKeys)
        Shortcuts::resetAll();
    for (auto it = m_pendingKeys.cbegin(); it != m_pendingKeys.cend(); ++it)
        Shortcuts::set(it.key(), it.value());
    UpdateChecker::setChannel(m_updatesOff->isChecked()   ? UpdateChecker::Channel::Off
                              : m_updatesPre->isChecked() ? UpdateChecker::Channel::PreReleases
                                                          : UpdateChecker::Channel::Releases);
}

// ---- Shortcuts ----

QWidget* SettingsDialog::buildShortcuts()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* intro = new QLabel("Pick something, then press the keys you'd like for it.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_keys = new QTableWidget(0, 2);
    m_keys->setHorizontalHeaderLabels({ "What it does", "Keys" });
    m_keys->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_keys->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_keys->verticalHeader()->setVisible(false);
    m_keys->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_keys->setSelectionMode(QAbstractItemView::SingleSelection);
    m_keys->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_keys, 1);

    // Underneath: the keys for whatever's picked
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("Keys"));
    m_keyEdit = new QKeySequenceEdit;
    m_keyEdit->setMaximumSequenceLength(1);
    m_keyEdit->setToolTip("Click here, then press the keys (like Ctrl+K)");
    row->addWidget(m_keyEdit, 1);
    auto* none = new QPushButton("None");
    none->setToolTip("No shortcut for this one");
    auto* normal = new QPushButton("Back to normal");
    row->addWidget(none);
    row->addWidget(normal);
    layout->addLayout(row);
    m_keyNote = new QLabel;
    m_keyNote->setWordWrap(true);
    m_keyNote->setProperty("role", "hint");
    layout->addWidget(m_keyNote);
    auto* all = new QPushButton("Put them all back to normal");
    layout->addWidget(all, 0, Qt::AlignLeft);

    const QList<Shortcuts::Entry> entries = Shortcuts::all();
    auto pickedRow = [this] { return m_keys->currentRow(); };
    auto change = [this, entries](int r, const QList<QKeySequence>& keys) {
        if (r < 0 || r >= entries.size())
            return;
        QString note;
        // Already someone else's? Then they lose it (and you're told)
        if (!keys.isEmpty())
            for (int other = 0; other < entries.size(); ++other)
                if (other != r && keysFor(other).contains(keys.first())) {
                    m_pendingKeys[entries[other].id] = {};
                    note = QString("%1 was on %2, so that's got no shortcut now.")
                               .arg(keys.first().toString(QKeySequence::NativeText), entries[other].name);
                }
        m_pendingKeys[entries[r].id] = keys;
        showKeys();
        m_keyNote->setText(note);
    };
    connect(m_keys, &QTableWidget::currentCellChanged, this, [this, entries](int r) {
        QSignalBlocker hush(m_keyEdit);
        m_keyEdit->setKeySequence(r >= 0 ? keysFor(r).value(0) : QKeySequence());
        m_keyNote->setText(r >= 0 ? QString("Normally: %1").arg(entries[r].defaults.isEmpty()
                                                                     ? QStringLiteral("none")
                                                                     : entries[r].defaults.first().toString(QKeySequence::NativeText))
                                  : QString());
    });
    connect(m_keyEdit, &QKeySequenceEdit::editingFinished, this, [=, this] {
        const QKeySequence k = m_keyEdit->keySequence();
        change(pickedRow(), k.isEmpty() ? QList<QKeySequence> {} : QList<QKeySequence> { k });
    });
    connect(none, &QPushButton::clicked, this, [=] { change(pickedRow(), {}); });
    connect(normal, &QPushButton::clicked, this, [=] {
        if (pickedRow() >= 0)
            change(pickedRow(), entries[pickedRow()].defaults);
    });
    connect(all, &QPushButton::clicked, this, [this] {
        m_pendingKeys.clear();
        m_resetAllKeys = true;
        showKeys();
        m_keyNote->setText("Everything's back to its usual keys (once you press OK).");
    });
    showKeys();
    return page;
}

QList<QKeySequence> SettingsDialog::keysFor(int row) const
{
    const QList<Shortcuts::Entry> entries = Shortcuts::all();
    if (row < 0 || row >= entries.size())
        return {};
    const Shortcuts::Entry& e = entries[row];
    if (m_pendingKeys.contains(e.id))
        return m_pendingKeys.value(e.id);
    return m_resetAllKeys ? e.defaults : e.action->shortcuts();
}

void SettingsDialog::showKeys()
{
    const QList<Shortcuts::Entry> entries = Shortcuts::all();
    const int keep = m_keys->currentRow();
    QSignalBlocker hush(m_keys);
    m_keys->setRowCount(int(entries.size()));
    for (int r = 0; r < entries.size(); ++r) {
        QStringList shown;
        for (const QKeySequence& k : keysFor(r))
            if (!k.isEmpty())
                shown << k.toString(QKeySequence::NativeText);
        auto* what = new QTableWidgetItem(entries[r].name);
        auto* keys = new QTableWidgetItem(shown.isEmpty() ? QStringLiteral("—") : shown.join("  or  "));
        // Changed from normal: in bold, so you can see what you've changed
        QFont f = keys->font();
        f.setBold(keysFor(r) != entries[r].defaults);
        keys->setFont(f);
        m_keys->setItem(r, 0, what);
        m_keys->setItem(r, 1, keys);
    }
    if (keep >= 0)
        m_keys->setCurrentCell(keep, 0);
}
