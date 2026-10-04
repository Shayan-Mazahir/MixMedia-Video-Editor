// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "TimelineClip.h"

#include <QWidget>

class NumberSlider;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QToolButton;

// The properties panel: shows whichever clip is selected and lets you tweak it.
// Every number has a slider for quick changes and a box for typing exact values.
class ClipInspector : public QWidget {
    Q_OBJECT

public:
    explicit ClipInspector(QWidget* parent = nullptr);

    // index -1 = nothing selected. part = (for transition blocks) VE_PART_*, or -1 when it's on a cut.
    void showClip(int index, const TimelineClip& clip, int part = -1);

    // Keyframes get added and changed wherever the playhead is
    void setPlayhead(double sec);
    // The spot to zoom into, picked by clicking the preview (-0.5..0.5 from the middle)
    void setSpot(QPointF spot);
    // The green screen colour, picked by clicking the preview
    void setKeyColor(const QColor& color);

signals:
    // `what` names the setting, so the timeline can merge slider drags into one undo step
    void edited(int index, const TimelineClip& clip, const QString& what);
    void speedChanged(int index, double speed);
    void detachAudioClicked();
    void seekRequested(double sec); // jump to a keyframe
    void pickSpotRequested();       // "pick it on the preview"
    void pickColorRequested();      // same, for the green screen colour
    void reverseChanged(int index, bool backwards);
    void freezeFrameClicked();
    void evenOutVolumeClicked(int index);

private:
    NumberSlider* addSlider(QFormLayout* form, const QString& label, const QString& what,
                            double min, double max, double sliderMin, double sliderMax,
                            double defaultValue, const QString& suffix = QString(), int decimals = 1);

    // A slider with a ◆ next to it for keyframes. `unit` = slider value for 1.0 of the setting.
    NumberSlider* addKeySlider(QFormLayout* form, const QString& label, const QString& what, int param, double unit,
                               double min, double max, double sliderMin, double sliderMax,
                               double defaultValue, const QString& suffix);

    void apply(const QString& what);
    double local() const { return m_playhead - m_clip.start; } // the playhead, in the clip's own time
    bool playheadOnClip() const { return local() >= -1e-6 && local() <= m_clip.duration + 1e-6; }
    void refreshKeys(); // keyframed sliders show the value at the playhead, diamonds show what's there
    void toggleKey(int param);
    void jumpToKey(int direction);
    void clearKeys();
    void applyZoomPan();

    void refreshColorButton();
    void refreshKeyColorButton();
    void resetEffects();
    void resetPlacement();
    void resetCrop();

    int m_index = -1;
    TimelineClip m_clip;
    double m_playhead = 0.0;

    struct KeyControl {
        int param;          // VE_KEY_*
        QString what;
        NumberSlider* slider;
        QToolButton* button;
        double unit;
    };
    QList<KeyControl> m_keyControls;
    QWidget* m_keyNav;
    QPushButton* m_prevKey;
    QPushButton* m_nextKey;
    QPushButton* m_clearKeys;

    QGroupBox* m_zoomBox;
    QComboBox* m_zoomPreset;
    QWidget* m_spotControls;
    NumberSlider *m_spotX, *m_spotY, *m_zoomAmount, *m_zoomTakes, *m_zoomHold;
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
    QCheckBox* m_italic;
    QFontComboBox* m_font;
    QComboBox* m_align;
    NumberSlider* m_textX;
    NumberSlider* m_spacing;
    NumberSlider* m_outline;
    QPushButton* m_outlineColor;
    QCheckBox* m_shadow;
    QPushButton* m_shadowColor;
    NumberSlider* m_shadowDistance;
    NumberSlider* m_shadowSoftness;
    QPushButton* m_boxColor;
    // Subtitle lines only
    QWidget* m_subtitleRows;
    QCheckBox* m_ownStyle;
    QLabel* m_lookHint;
    QCheckBox* m_wordByWord;
    QPushButton* m_highlight;
    NumberSlider* m_wordsAtOnce;
    QPushButton* colorButton(QColor TitleStyle::*which, const QString& title, bool seeThrough);

    QGroupBox* m_speedBox;
    QComboBox* m_speedPresets;
    QDoubleSpinBox* m_speed;
    QCheckBox* m_reverse;
    QCheckBox* m_keepPitch;
    QPushButton* m_evenOut;
    NumberSlider* m_denoise;
    QCheckBox* m_duck;
    NumberSlider* m_duckAmount;
    QPushButton* m_freeze;

    QGroupBox* m_keyBox;
    QCheckBox* m_keyOn;
    QPushButton* m_keyColor;
    NumberSlider *m_keyStrength, *m_keySoftness, *m_keySpill;

    QGroupBox* m_effectsBox;
    QComboBox* m_look;
    NumberSlider* m_strength; // for effect blocks: how strongly they apply
    NumberSlider *m_brightness, *m_contrast, *m_saturation, *m_temperature, *m_blur, *m_sharpen, *m_vignette;

    QGroupBox* m_placeBox;
    NumberSlider *m_scale, *m_posX, *m_posY, *m_opacity;

    QGroupBox* m_cropBox;
    QCheckBox* m_fill;
    NumberSlider* m_rotation;
    QPushButton* m_flipH;
    QPushButton* m_flipV;
    NumberSlider *m_cropLeft, *m_cropRight, *m_cropTop, *m_cropBottom;

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
