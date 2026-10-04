// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "SettingsDialog.h"
#include "AppSettings.h"
#include "HelpWindow.h"
#include "NumberSlider.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
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
    intro->setStyleSheet("color: #808286;");
    layout->addWidget(intro);
    m_advanced = new QCheckBox("Let me choose (advanced)");
    layout->addWidget(m_advanced);

    m_details = new QGroupBox("How much to use");
    auto* form = new QFormLayout(m_details);
    const int cores = ve_cpu_threads();
    m_threads = new NumberSlider(1, cores, 1, cores, 0, cores);
    form->addRow("CPU threads", m_threads);
    m_threadsNote = new QLabel;
    m_threadsNote->setStyleSheet("color: #808286; font-size: 11px;");
    m_threadsNote->setWordWrap(true);
    form->addRow(m_threadsNote);
    m_gpuExport = new QCheckBox("Use the graphics card for exporting");
    m_gpuCaptions = new QCheckBox("Use the graphics card for auto-captions");
    form->addRow(m_gpuExport);
    form->addRow(m_gpuCaptions);
    layout->addWidget(m_details);
    layout->addStretch();

    QSettings s;
    m_advanced->setChecked(AppSettings::advancedPerformance());
    m_threads->setValue(AppSettings::threads());
    m_gpuExport->setChecked(s.value("performance/gpuExport", true).toBool());
    m_gpuCaptions->setChecked(s.value("performance/gpuCaptions", true).toBool());
    connect(m_advanced, &QCheckBox::toggled, this, &SettingsDialog::refresh);
    connect(m_threads, &NumberSlider::valueChanged, this, &SettingsDialog::refresh);
    refresh();

    auto* tabs = new QTabWidget;
    tabs->addTab(performance, "Performance");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    connect(buttons, &QDialogButtonBox::helpRequested, [] { HelpWindow::open("settings.md"); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* outer = new QVBoxLayout(this);
    outer->addWidget(tabs);
    outer->addWidget(buttons);
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
    QSettings s;
    s.setValue("performance/advanced", m_advanced->isChecked());
    s.setValue("performance/threads", int(m_threads->value()));
    s.setValue("performance/gpuExport", m_gpuExport->isChecked());
    s.setValue("performance/gpuCaptions", m_gpuCaptions->isChecked());
    AppSettings::apply();
}
