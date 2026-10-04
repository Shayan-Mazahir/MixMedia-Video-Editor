// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "ExportDialog.h"

#include "AppSettings.h"
#include "HelpWindow.h"

#include <ve/engine.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>

#include <cmath>

namespace {

QString sizeText(QSize s) { return QString("%1×%2").arg(s.width()).arg(s.height()); }

bool isSound(int format) { return format == VE_FORMAT_MP3 || format == VE_FORMAT_M4A; }

} // namespace

QString ExportDialog::extensionFor(int format)
{
    switch (format) {
    case VE_FORMAT_GIF: return "gif";
    case VE_FORMAT_MP3: return "mp3";
    case VE_FORMAT_M4A: return "m4a";
    default: return "mp4";
    }
}

ExportDialog::ExportDialog(QSize projectSize, double projectFps, const QString& suggestedPath,
                           bool canCopy, const QString& whyNotCopy, bool hasSubtitles, QWidget* parent)
    : QDialog(parent)
    , m_projectSize(projectSize)
    , m_projectFps(projectFps)
    , m_canCopy(canCopy)
    , m_hasSubtitles(hasSubtitles)
{
    setWindowTitle("Export");
    setMinimumWidth(500);

    const bool mp3 = ve_export_format_available(VE_FORMAT_MP3);
    const double aspect = double(projectSize.width()) / projectSize.height();
    const QSize gif(480, int(std::lround(480 / aspect)) & ~1);
    m_presets = {
        { "Same as the project", VE_FORMAT_MP4, {}, 0, 21 },
        { "YouTube 1080p", VE_FORMAT_MP4, { 1920, 1080 }, 0, 18 },
        { "YouTube 4K", VE_FORMAT_MP4, { 3840, 2160 }, 0, 18 },
        { "Shorts / TikTok / Reels (tall)", VE_FORMAT_MP4, { 1080, 1920 }, 30, 18 },
        { "Instagram square", VE_FORMAT_MP4, { 1080, 1080 }, 30, 18 },
        { "Small file to send", VE_FORMAT_MP4, { 1280, 720 }, 30, 26 },
        { "GIF", VE_FORMAT_GIF, gif, 15, 0 },
        { mp3 ? "Sound only (MP3)" : "Sound only (M4A)", mp3 ? VE_FORMAT_MP3 : VE_FORMAT_M4A, {}, 0, 0 },
    };

    m_path = new QLineEdit(suggestedPath);
    auto* browseButton = new QPushButton("Browse…");
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseButton);

    m_preset = new QComboBox;
    for (const Preset& p : m_presets)
        m_preset->addItem(p.name);
    m_preset->addItem("Custom");

    m_format = new QComboBox;
    m_format->addItem("MP4 video", VE_FORMAT_MP4);
    m_format->addItem("GIF (moving picture, no sound)", VE_FORMAT_GIF);
    if (mp3)
        m_format->addItem("MP3 (sound only)", VE_FORMAT_MP3);
    m_format->addItem("M4A (sound only)", VE_FORMAT_M4A);

    m_resolution = new QComboBox;
    m_resolution->addItem(QString("Same as project (%1)").arg(sizeText(projectSize)), projectSize);
    const QList<QSize> sizes = { { 3840, 2160 }, { 2560, 1440 }, { 1920, 1080 }, { 1280, 720 }, { 854, 480 },
                                 { 1080, 1920 }, { 720, 1280 }, { 1080, 1080 }, gif, { 320, int(std::lround(320 / aspect)) & ~1 } };
    for (QSize s : sizes) {
        if (s == projectSize)
            continue;
        QString label = sizeText(s);
        if (s.height() > s.width())
            label += "  (tall)";
        else if (s.height() == s.width())
            label += "  (square)";
        m_resolution->addItem(label, s);
    }
    m_shapeNote = new QLabel("A different shape from your project, so you'll get black bars around the picture.");
    m_shapeNote->setWordWrap(true);
    m_shapeNote->setProperty("role", "hint");
    auto* resolutionBox = new QVBoxLayout;
    resolutionBox->addWidget(m_resolution);
    resolutionBox->addWidget(m_shapeNote);

    m_frameRate = new QComboBox;
    m_frameRate->addItem(QString("Same as project (%1 fps)").arg(projectFps, 0, 'g', 4), projectFps);
    for (double f : { 60.0, 30.0, 24.0, 15.0, 10.0 })
        if (std::abs(f - projectFps) > 0.01)
            m_frameRate->addItem(QString("%1 fps").arg(f), f);

    m_quality = new QComboBox;
    m_quality->addItem("High", 18);
    m_quality->addItem("Normal", 21);
    m_quality->addItem("Small file", 26);

    m_graphicsCard = new QCheckBox("Use the graphics card (much faster)");
    m_graphicsCard->setChecked(AppSettings::gpuForExport());
    m_graphicsCard->setToolTip("Falls back to the CPU automatically if your graphics card can't do it");

    // Instant = copy the video as it is. Normal = rebuild every frame (needed for titles, fades, ...)
    m_instant = new QRadioButton("Instant: copy without re-encoding");
    m_normal = new QRadioButton("Normal: re-encode everything");
    auto* modes = new QButtonGroup(this);
    modes->addButton(m_instant);
    modes->addButton(m_normal);
    m_copyNote = new QLabel;
    m_copyNote->setWordWrap(true);
    m_copyNote->setProperty("role", "hint");
    if (canCopy) {
        m_copyNote->setText("Instant takes seconds. Cuts snap to the nearest keyframe, so a piece may start a moment early.");
    } else {
        m_copyNote->setText("Instant isn't available: " + whyNotCopy);
        m_instant->setEnabled(false);
    }
    m_normal->setChecked(true);
    auto* modeBox = new QVBoxLayout;
    modeBox->addWidget(m_instant);
    modeBox->addWidget(m_normal);
    modeBox->addWidget(m_copyNote);

    auto* form = m_form = new QFormLayout;
    form->addRow("Save to", pathRow);
    form->addRow("Preset", m_preset);
    form->addRow("Format", m_format);
    form->addRow("How", modeBox);
    form->addRow("Resolution", resolutionBox);
    form->addRow("Frame rate", m_frameRate);
    form->addRow("Quality", m_quality);
    form->addRow("", m_graphicsCard);

    // Subtitles: any mix of the three (remembered for next time)
    QSettings remembered;
    m_burnSubs = new QCheckBox("In the picture (always showing)");
    m_srtSubs = new QCheckBox("As an .srt file next to the video");
    m_trackSubs = new QCheckBox("As a track viewers can switch on and off");
    m_burnSubs->setChecked(remembered.value("export/burnSubtitles", true).toBool());
    m_srtSubs->setChecked(remembered.value("export/srtSubtitles", false).toBool());
    m_trackSubs->setChecked(remembered.value("export/trackSubtitles", false).toBool());
    m_trackSubs->setToolTip("Works in most players (VLC, phones, browsers). YouTube wants the .srt file instead.");
    auto* subs = new QVBoxLayout;
    for (QCheckBox* b : { m_burnSubs, m_srtSubs, m_trackSubs })
        subs->addWidget(b);
    form->addRow("Subtitles", subs);
    for (QCheckBox* b : { m_burnSubs, m_srtSubs, m_trackSubs })
        connect(b, &QCheckBox::toggled, this, &ExportDialog::refresh);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    buttons->button(QDialogButtonBox::Ok)->setText("Export");
    connect(buttons, &QDialogButtonBox::helpRequested, [] { HelpWindow::open("export.md"); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings s;
        s.setValue("export/preset", m_preset->currentText());
        if (m_hasSubtitles) {
            s.setValue("export/burnSubtitles", m_burnSubs->isChecked());
            s.setValue("export/srtSubtitles", m_srtSubs->isChecked());
            s.setValue("export/trackSubtitles", m_trackSubs->isChecked());
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_preset, &QComboBox::currentIndexChanged, this, &ExportDialog::applyPreset);
    for (QComboBox* box : { m_format, m_resolution, m_frameRate, m_quality })
        connect(box, &QComboBox::currentIndexChanged, this, [this] {
            markCustom();
            refresh();
        });
    connect(m_normal, &QRadioButton::toggled, this, &ExportDialog::refresh);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    // Start where you left off last time (or plain "same as the project")
    int last = m_preset->findText(QSettings().value("export/preset").toString());
    m_preset->setCurrentIndex(last >= 0 && last < m_presets.size() ? last : 0);
    applyPreset(m_preset->currentIndex());
}

void ExportDialog::applyPreset(int index)
{
    if (index < 0 || index >= m_presets.size())
        return; // "Custom" keeps whatever's there
    const Preset& p = m_presets[index];
    m_applyingPreset = true;
    selectData(m_format, p.format);
    selectSize(p.size.isEmpty() ? m_projectSize : p.size);
    selectData(m_frameRate, p.fps > 0 ? p.fps : m_projectFps);
    if (p.crf > 0)
        selectData(m_quality, p.crf);
    // A simple cut kept as it is? Instant's the obvious pick. Any other preset means re-encoding.
    (index == 0 && m_canCopy ? m_instant : m_normal)->setChecked(true);
    m_applyingPreset = false;
    refresh();
}

void ExportDialog::markCustom()
{
    if (!m_applyingPreset && m_preset->currentIndex() != m_presets.size()) {
        QSignalBlocker block(m_preset);
        m_preset->setCurrentIndex(int(m_presets.size())); // "Custom"
    }
}

void ExportDialog::selectSize(QSize size)
{
    int i = m_resolution->findData(size);
    if (i < 0) {
        m_resolution->addItem(sizeText(size), size);
        i = m_resolution->count() - 1;
    }
    m_resolution->setCurrentIndex(i);
}

void ExportDialog::selectData(QComboBox* box, const QVariant& value)
{
    for (int i = 0; i < box->count(); ++i) {
        QVariant d = box->itemData(i);
        bool same = value.typeId() == QMetaType::Double ? std::abs(d.toDouble() - value.toDouble()) < 0.01 : d == value;
        if (same) {
            box->setCurrentIndex(i);
            return;
        }
    }
}

void ExportDialog::refresh()
{
    const int f = format();
    const bool mp4 = f == VE_FORMAT_MP4;
    const bool picture = !isSound(f);

    // Only show what matters for this format, and grey out what instant export ignores
    m_instant->setEnabled(m_canCopy);
    if (!mp4 && m_instant->isChecked())
        m_normal->setChecked(true);
    const bool reencode = !mp4 || m_normal->isChecked();
    m_form->setRowVisible(3, mp4);              // How
    m_form->setRowVisible(4, picture);          // Resolution
    m_form->setRowVisible(5, picture);          // Frame rate
    m_form->setRowVisible(6, mp4);              // Quality
    m_form->setRowVisible(7, mp4);              // Graphics card
    m_form->setRowVisible(8, m_hasSubtitles);   // Subtitles
    m_burnSubs->setVisible(picture);
    m_trackSubs->setVisible(mp4);
    // Subtitles in the picture (or as a track) mean redrawing every frame, so no instant copy
    if (m_hasSubtitles && mp4 && (m_burnSubs->isChecked() || m_trackSubs->isChecked()) && m_instant->isChecked())
        m_normal->setChecked(true);
    m_resolution->setEnabled(reencode);
    m_frameRate->setEnabled(reencode);
    m_quality->setEnabled(reencode);
    m_graphicsCard->setEnabled(reencode);
    adjustSize(); // shrink or grow to fit the rows that are showing

    QSize s = size();
    double projectShape = double(m_projectSize.width()) / m_projectSize.height();
    m_shapeNote->setVisible(picture && reencode && std::abs(double(s.width()) / s.height() - projectShape) > 0.02);

    // Keep the file name's ending in step with the format
    QString path = m_path->text().trimmed();
    QFileInfo info(path);
    QString wanted = extensionFor(f);
    if (!path.isEmpty() && info.suffix().compare(wanted, Qt::CaseInsensitive) != 0) {
        QString known = info.suffix().toLower();
        if (known == "mp4" || known == "gif" || known == "mp3" || known == "m4a")
            path.chop(known.size() + 1);
        m_path->setText(path + "." + wanted);
    }
}

void ExportDialog::browse()
{
    static const char* filters[] = { "MP4 video (*.mp4)", "GIF (*.gif)", "MP3 sound (*.mp3)", "M4A sound (*.m4a)" };
    QString chosen = QFileDialog::getSaveFileName(this, "Export to", m_path->text(), filters[format()]);
    if (!chosen.isEmpty()) {
        m_path->setText(chosen);
        refresh(); // (adds the ending if it was left off)
    }
}

QString ExportDialog::path() const
{
    QString p = m_path->text().trimmed();
    QString ext = "." + extensionFor(format());
    if (!p.isEmpty() && !p.endsWith(ext, Qt::CaseInsensitive))
        p += ext;
    return p;
}

int ExportDialog::format() const
{
    return m_format->currentData().toInt();
}

QSize ExportDialog::size() const
{
    QSize s = m_resolution->currentData().toSize();
    if (format() == VE_FORMAT_GIF)
        return s;
    return QSize(s.width() & ~1, s.height() & ~1); // video encoders want even numbers
}

double ExportDialog::fps() const
{
    return m_frameRate->currentData().toDouble();
}

int ExportDialog::crf() const
{
    return m_quality->currentData().toInt();
}

bool ExportDialog::instant() const
{
    return format() == VE_FORMAT_MP4 && m_instant->isChecked();
}

bool ExportDialog::burnSubtitles() const
{
    return m_hasSubtitles && format() != VE_FORMAT_MP3 && format() != VE_FORMAT_M4A && m_burnSubs->isChecked();
}

bool ExportDialog::subtitleFile() const
{
    return m_hasSubtitles && m_srtSubs->isChecked();
}

bool ExportDialog::subtitleTrack() const
{
    return m_hasSubtitles && format() == VE_FORMAT_MP4 && m_trackSubs->isChecked();
}

bool ExportDialog::useGraphicsCard() const
{
    return m_graphicsCard->isChecked();
}
