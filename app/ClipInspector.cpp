// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "ClipInspector.h"
#include "NumberSlider.h"

#include <ve/engine.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>

namespace {

QString formatSeconds(double s)
{
    int cs = int(s * 100 + 0.5);
    return QString::asprintf("%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

QDoubleSpinBox* makeSecondsBox(double max)
{
    auto* box = new QDoubleSpinBox;
    box->setRange(0.0, max);
    box->setSingleStep(0.25);
    box->setDecimals(2);
    box->setSuffix(" s");
    box->setKeyboardTracking(false);
    return box;
}

QPushButton* makeResetButton(const QString& text)
{
    auto* b = new QPushButton(text);
    b->setFlat(true);
    b->setStyleSheet("color: #2fc6b4;");
    return b;
}

const double SpeedPresets[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0 };

} // namespace

ClipInspector::ClipInspector(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 8, 8, 8);

    auto* title = new QLabel("Properties");
    title->setStyleSheet("font-weight: bold;");
    outer->addWidget(title);

    m_empty = new QLabel("Click a clip on the timeline to tweak it here.");
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet("color: #808286;");
    m_empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    outer->addWidget(m_empty);

    // Lots of settings, so they scroll
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_panel = new QWidget;
    m_scroll->setWidget(m_panel);
    outer->addWidget(m_scroll, 1);

    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(0, 0, 4, 0);

    m_name = new QLabel;
    m_name->setWordWrap(true);
    m_name->setStyleSheet("font-weight: bold; color: #2fc6b4;");
    m_info = new QLabel;
    m_info->setStyleSheet("color: #808286;");
    auto* hint = new QLabel("Drag a slider or type an exact number. Double-click a slider to reset it.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #808286; font-size: 11px;");
    layout->addWidget(m_name);
    layout->addWidget(m_info);
    layout->addWidget(hint);

    // ---- Title ----
    m_titleBox = new QGroupBox("Text");
    auto* titleForm = new QFormLayout(m_titleBox);
    m_text = new QPlainTextEdit;
    m_text->setMaximumHeight(70);
    titleForm->addRow(m_text);
    m_textSize = addSlider(titleForm, "Size", "textSize", 1, 60, 2, 40, 8, " %");
    m_textSize->setToolTip("Size, as a percentage of the picture's height");
    m_textY = addSlider(titleForm, "Height", "textY", 0, 100, 0, 100, 82, " %");
    m_textY->setToolTip("0% = top of the picture, 100% = bottom");
    m_color = new QPushButton;
    m_background = new QCheckBox("Dark box behind it");
    m_bold = new QCheckBox("Bold");
    titleForm->addRow("Colour", m_color);
    titleForm->addRow(m_background);
    titleForm->addRow(m_bold);
    layout->addWidget(m_titleBox);

    // ---- Speed ----
    m_speedBox = new QGroupBox("Speed");
    auto* speedForm = new QFormLayout(m_speedBox);
    m_speedPresets = new QComboBox;
    m_speedPresets->addItem("Presets…", 0.0);
    for (double s : SpeedPresets)
        m_speedPresets->addItem(s == 1.0 ? QStringLiteral("1× (normal)") : QString("%1×").arg(s), s);
    m_speed = new QDoubleSpinBox;
    m_speed->setRange(0.1, 10.0);
    m_speed->setDecimals(2);
    m_speed->setSingleStep(0.05);
    m_speed->setSuffix("×");
    m_speed->setKeyboardTracking(false);
    m_speed->setToolTip("Below 1× is slow motion, above is fast forward. The sound changes pitch to match.");
    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(m_speed, 1);
    speedRow->addWidget(m_speedPresets, 1);
    speedForm->addRow("Play at", speedRow);
    layout->addWidget(m_speedBox);

    // ---- Effects ----
    m_effectsBox = new QGroupBox("Effects");
    auto* fxForm = new QFormLayout(m_effectsBox);
    m_look = new QComboBox;
    const char* looks[] = { "None", "Black & white", "Sepia", "Vintage", "Vivid", "Cool", "Warm", "Faded", "Dramatic" };
    for (int i = 0; i < VE_LOOK_COUNT; ++i)
        m_look->addItem(looks[i], i);
    fxForm->addRow("Look", m_look);
    m_brightness = addSlider(fxForm, "Brightness", "brightness", -100, 100, -100, 100, 0);
    m_contrast = addSlider(fxForm, "Contrast", "contrast", -100, 100, -100, 100, 0);
    m_saturation = addSlider(fxForm, "Colour", "saturation", -100, 100, -100, 100, 0);
    m_temperature = addSlider(fxForm, "Warmth", "temperature", -100, 100, -100, 100, 0);
    m_blur = addSlider(fxForm, "Blur", "blur", 0, 100, 0, 100, 0);
    m_sharpen = addSlider(fxForm, "Sharpen", "sharpen", 0, 100, 0, 100, 0);
    m_vignette = addSlider(fxForm, "Vignette", "vignette", 0, 100, 0, 100, 0);
    QPushButton* resetFx = makeResetButton("Reset effects");
    fxForm->addRow(resetFx);
    layout->addWidget(m_effectsBox);

    // ---- Position & size (picture-in-picture) ----
    m_placeBox = new QGroupBox("Position && size");
    auto* placeForm = new QFormLayout(m_placeBox);
    // The sliders cover the handy range; type a number to go further (e.g. fully off screen)
    m_scale = addSlider(placeForm, "Size", "scale", 5, 400, 10, 200, 100, " %");
    m_posX = addSlider(placeForm, "Left / right", "posX", -200, 200, -100, 100, 0, " %");
    m_posY = addSlider(placeForm, "Up / down", "posY", -200, 200, -100, 100, 0, " %");
    m_opacity = addSlider(placeForm, "Opacity", "opacity", 0, 100, 0, 100, 100, " %");
    QPushButton* resetPlace = makeResetButton("Reset position && size");
    placeForm->addRow(resetPlace);
    layout->addWidget(m_placeBox);

    // ---- Sound ----
    m_soundBox = new QGroupBox("Sound");
    auto* soundForm = new QFormLayout(m_soundBox);
    m_volume = addSlider(soundForm, "Volume", "volume", 0, 400, 0, 200, 100, " %");
    m_detach = new QPushButton("Detach audio");
    m_detach->setToolTip("Put this clip's sound on its own audio track");
    soundForm->addRow(m_detach);
    layout->addWidget(m_soundBox);

    // ---- Fades ----
    m_fadeBox = new QGroupBox("Fade");
    auto* fadeForm = new QFormLayout(m_fadeBox);
    m_fadeIn = makeSecondsBox(60);
    m_fadeOut = makeSecondsBox(60);
    fadeForm->addRow("In", m_fadeIn);
    fadeForm->addRow("Out", m_fadeOut);
    auto* fadeHint = new QLabel("Tip: overlap two clips on different video tracks and fade the top one in for a cross-dissolve.");
    fadeHint->setWordWrap(true);
    fadeHint->setStyleSheet("color: #808286; font-size: 11px;");
    fadeForm->addRow(fadeHint);
    layout->addWidget(m_fadeBox);
    layout->addStretch();

    connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, [this] { apply("fadeIn"); });
    connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, [this] { apply("fadeOut"); });
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] { apply("text"); });
    connect(m_background, &QCheckBox::toggled, this, [this] { apply("textBox"); });
    connect(m_bold, &QCheckBox::toggled, this, [this] { apply("textBold"); });
    connect(m_color, &QPushButton::clicked, this, &ClipInspector::pickColor);
    connect(m_detach, &QPushButton::clicked, this, &ClipInspector::detachAudioClicked);
    connect(m_look, &QComboBox::currentIndexChanged, this, [this] { apply("look"); });
    connect(resetFx, &QPushButton::clicked, this, &ClipInspector::resetEffects);
    connect(resetPlace, &QPushButton::clicked, this, &ClipInspector::resetPlacement);
    connect(m_speed, &QDoubleSpinBox::valueChanged, this, [this](double s) {
        if (!m_loading && m_index >= 0)
            emit speedChanged(m_index, s);
    });
    connect(m_speedPresets, &QComboBox::activated, this, [this] {
        double s = m_speedPresets->currentData().toDouble();
        m_speedPresets->setCurrentIndex(0); // it's a shortcut menu, not a setting
        if (s > 0)
            m_speed->setValue(s);
    });

    showClip(-1, {});
}

NumberSlider* ClipInspector::addSlider(QFormLayout* form, const QString& label, const QString& what,
                                       double min, double max, double sliderMin, double sliderMax,
                                       double defaultValue, const QString& suffix, int decimals)
{
    auto* s = new NumberSlider(min, max, sliderMin, sliderMax, decimals, defaultValue, suffix);
    form->addRow(label, s);
    connect(s, &NumberSlider::valueChanged, this, [this, what] { apply(what); });
    return s;
}

void ClipInspector::showClip(int index, const TimelineClip& clip)
{
    m_index = index;
    m_clip = clip;
    m_empty->setVisible(index < 0);
    m_scroll->setVisible(index >= 0);
    if (index < 0)
        return;

    m_loading = true;
    m_name->setText(clip.isTitle() ? QStringLiteral("Title") : clip.name);
    QString info = QString("Starts at %1 · %2 long").arg(formatSeconds(clip.start), formatSeconds(clip.duration));
    if (clip.speed != 1.0)
        info += QString(" · %1×").arg(clip.speed);
    m_info->setText(info);

    m_titleBox->setVisible(clip.isTitle());
    if (clip.isTitle()) {
        if (m_text->toPlainText() != clip.title.text)
            m_text->setPlainText(clip.title.text);
        m_textSize->setValue(clip.title.size);
        m_textY->setValue(clip.title.y * 100);
        m_background->setChecked(clip.title.box);
        m_bold->setChecked(clip.title.bold);
        refreshColorButton();
    }

    // Only show what makes sense for this kind of clip
    bool video = clip.showsVideo() && !clip.isTitle();
    m_speedBox->setVisible(!clip.isTitle() && !clip.isStill());
    m_effectsBox->setVisible(video);
    m_placeBox->setVisible(clip.showsVideo());
    m_soundBox->setVisible(clip.hasAudio && clip.audioOn);
    m_detach->setVisible(clip.showsVideo() && clip.playsAudio());

    m_speed->setValue(clip.speed);
    m_look->setCurrentIndex(std::clamp(clip.look, 0, VE_LOOK_COUNT - 1));
    m_brightness->setValue(clip.brightness * 100);
    m_contrast->setValue(clip.contrast * 100);
    m_saturation->setValue(clip.saturation * 100);
    m_temperature->setValue(clip.temperature * 100);
    m_blur->setValue(clip.blur * 100);
    m_sharpen->setValue(clip.sharpen * 100);
    m_vignette->setValue(clip.vignette * 100);
    m_scale->setValue(clip.scale * 100);
    m_posX->setValue(clip.posX * 100);
    m_posY->setValue(clip.posY * 100);
    m_opacity->setValue(clip.opacity * 100);
    m_volume->setValue(clip.volume * 100);
    m_fadeIn->setValue(clip.fadeIn);
    m_fadeOut->setValue(clip.fadeOut);
    m_loading = false;
}

void ClipInspector::apply(const QString& what)
{
    if (m_loading || m_index < 0)
        return;

    m_clip.volume = float(m_volume->value() / 100);
    // A fade can't be longer than the clip itself
    m_clip.fadeIn = std::min(m_fadeIn->value(), m_clip.duration);
    m_clip.fadeOut = std::min(m_fadeOut->value(), m_clip.duration);

    m_clip.look = m_look->currentData().toInt();
    m_clip.brightness = float(m_brightness->value() / 100);
    m_clip.contrast = float(m_contrast->value() / 100);
    m_clip.saturation = float(m_saturation->value() / 100);
    m_clip.temperature = float(m_temperature->value() / 100);
    m_clip.blur = float(m_blur->value() / 100);
    m_clip.sharpen = float(m_sharpen->value() / 100);
    m_clip.vignette = float(m_vignette->value() / 100);

    m_clip.scale = float(m_scale->value() / 100);
    m_clip.posX = float(m_posX->value() / 100);
    m_clip.posY = float(m_posY->value() / 100);
    m_clip.opacity = float(m_opacity->value() / 100);

    if (m_clip.isTitle()) {
        m_clip.title.text = m_text->toPlainText();
        m_clip.title.size = m_textSize->value();
        m_clip.title.y = m_textY->value() / 100;
        m_clip.title.box = m_background->isChecked();
        m_clip.title.bold = m_bold->isChecked();
        QString first = m_clip.title.text.section('\n', 0, 0).trimmed();
        m_clip.name = first.isEmpty() ? QStringLiteral("Title") : first;
    }
    emit edited(m_index, m_clip, what);
}

void ClipInspector::resetEffects()
{
    m_loading = true;
    m_look->setCurrentIndex(0);
    for (NumberSlider* s : { m_brightness, m_contrast, m_saturation, m_temperature, m_blur, m_sharpen, m_vignette })
        s->setValue(0);
    m_loading = false;
    apply("resetEffects");
}

void ClipInspector::resetPlacement()
{
    m_loading = true;
    m_scale->setValue(100);
    m_posX->setValue(0);
    m_posY->setValue(0);
    m_opacity->setValue(100);
    m_loading = false;
    apply("resetPlacement");
}

void ClipInspector::pickColor()
{
    QColor c = QColorDialog::getColor(m_clip.title.color, this, "Text colour");
    if (!c.isValid())
        return;
    m_clip.title.color = c;
    refreshColorButton();
    apply("textColor");
}

void ClipInspector::refreshColorButton()
{
    m_color->setText(m_clip.title.color.name());
    m_color->setStyleSheet(QString("QPushButton { background: %1; color: %2; }")
                               .arg(m_clip.title.color.name(),
                                    m_clip.title.color.lightness() > 128 ? "black" : "white"));
}
