#pragma once

#include "TimelineClip.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;

// The properties panel: shows whichever clip is selected and lets you tweak it.
class ClipInspector : public QWidget {
    Q_OBJECT

public:
    explicit ClipInspector(QWidget* parent = nullptr);

    // index -1 = nothing selected
    void showClip(int index, const TimelineClip& clip);

signals:
    // `what` names the setting, so the timeline can merge slider drags into one undo step
    void edited(int index, const TimelineClip& clip, const QString& what);
    void detachAudioClicked();

private:
    void apply(const QString& what);
    void pickColor();
    void refreshColorButton();

    int m_index = -1;
    TimelineClip m_clip;
    bool m_loading = false; // don't treat filling in the controls as edits

    QLabel* m_empty;
    QWidget* m_panel;
    QLabel* m_name;
    QLabel* m_info;

    QGroupBox* m_soundBox;
    QSlider* m_volume;
    QLabel* m_volumeLabel;
    QPushButton* m_detach;

    QDoubleSpinBox* m_fadeIn;
    QDoubleSpinBox* m_fadeOut;

    QGroupBox* m_titleBox;
    QPlainTextEdit* m_text;
    QSpinBox* m_textSize;
    QSlider* m_textY;
    QPushButton* m_color;
    QCheckBox* m_background;
    QCheckBox* m_bold;
};
