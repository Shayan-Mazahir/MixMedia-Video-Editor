// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "TimelineClip.h"

#include <QWidget>

class NumberSlider;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;

// The properties panel: shows whichever clip is selected and lets you tweak it.
// Every number has a slider for quick changes and a box for typing exact values.
class ClipInspector : public QWidget {
    Q_OBJECT

public:
    explicit ClipInspector(QWidget* parent = nullptr);

    // index -1 = nothing selected. part = (for transition blocks) VE_PART_*, or -1 when it's on a cut.
    void showClip(int index, const TimelineClip& clip, int part = -1);

signals:
    // `what` names the setting, so the timeline can merge slider drags into one undo step
    void edited(int index, const TimelineClip& clip, const QString& what);
    void speedChanged(int index, double speed);
    void detachAudioClicked();

private:
    NumberSlider* addSlider(QFormLayout* form, const QString& label, const QString& what,
                            double min, double max, double sliderMin, double sliderMax,
                            double defaultValue, const QString& suffix = QString(), int decimals = 1);

    void apply(const QString& what);
    void pickColor();
    void refreshColorButton();
    void resetEffects();
    void resetPlacement();

    int m_index = -1;
    TimelineClip m_clip;
    bool m_loading = false; // don't treat filling in the controls as edits

    QLabel* m_empty;
    QScrollArea* m_scroll;
    QWidget* m_panel;
    QLabel* m_name;
    QLabel* m_info;

    QGroupBox* m_titleBox;
    QPlainTextEdit* m_text;
    NumberSlider* m_textSize;
    NumberSlider* m_textY;
    QPushButton* m_color;
    QCheckBox* m_background;
    QCheckBox* m_bold;

    QGroupBox* m_speedBox;
    QComboBox* m_speedPresets;
    QDoubleSpinBox* m_speed;

    QGroupBox* m_effectsBox;
    QComboBox* m_look;
    NumberSlider* m_strength; // for effect blocks: how strongly they apply
    NumberSlider *m_brightness, *m_contrast, *m_saturation, *m_temperature, *m_blur, *m_sharpen, *m_vignette;

    QGroupBox* m_placeBox;
    NumberSlider *m_scale, *m_posX, *m_posY, *m_opacity;

    QGroupBox* m_soundBox;
    NumberSlider* m_volume;
    QPushButton* m_detach;

    QGroupBox* m_transitionBox;
    QComboBox* m_transition;
    NumberSlider* m_transitionDuration;
    QLabel* m_transitionHint;

    QGroupBox* m_animBox;
    QComboBox* m_animIn;
    QComboBox* m_animOut;
    NumberSlider* m_animInDuration;
    NumberSlider* m_animOutDuration;

    QGroupBox* m_fadeBox;
    QDoubleSpinBox* m_fadeIn;
    QDoubleSpinBox* m_fadeOut;
};
