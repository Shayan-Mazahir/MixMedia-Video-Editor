// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QDialog>
#include <QSize>

class QCheckBox;
class QFormLayout;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(QSize projectSize, double projectFps, const QString& suggestedPath,
                 bool canCopy, const QString& whyNotCopy, bool hasSubtitles = false, QWidget* parent = nullptr);

    QString path() const;
    int format() const; // VE_FORMAT_*
    QSize size() const;
    double fps() const;
    int crf() const;
    bool useGraphicsCard() const;
    bool instant() const; // copy without re-encoding
    // Subtitles: in the picture, as an .srt next to the video, and/or a track players can switch on
    bool burnSubtitles() const;
    bool subtitleFile() const;
    bool subtitleTrack() const;

    static QString extensionFor(int format);

private:
    struct Preset {
        QString name;
        int format;
        QSize size;     // empty = same as the project
        double fps;     // 0 = same as the project
        int crf;
    };

    void browse();
    void applyPreset(int index);
    void refresh();        // greys out what doesn't apply, fixes the file extension
    void markCustom();     // a setting got changed by hand
    void selectSize(QSize size);
    void selectData(QComboBox* box, const QVariant& value);

    QSize m_projectSize;
    double m_projectFps;
    bool m_canCopy;
    bool m_applyingPreset = false;
    QList<Preset> m_presets;

    QFormLayout* m_form;
    QLineEdit* m_path;
    QComboBox* m_preset;
    QComboBox* m_format;
    QComboBox* m_resolution;
    QComboBox* m_frameRate;
    QComboBox* m_quality;
    QCheckBox* m_graphicsCard;
    QRadioButton* m_instant;
    QRadioButton* m_normal;
    QLabel* m_copyNote;
    QLabel* m_shapeNote;
    QCheckBox* m_burnSubs = nullptr;
    QCheckBox* m_srtSubs = nullptr;
    QCheckBox* m_trackSubs = nullptr;
    bool m_hasSubtitles;
};
