// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QSlider;

// A slider with a number box next to it, kept in sync both ways.
// Drag for quick changes, type for exact ones, double-click the slider to reset.
class NumberSlider : public QWidget {
    Q_OBJECT

public:
    // The slider covers [sliderMin, sliderMax]; typing can go out to [min, max]
    NumberSlider(double min, double max, double sliderMin, double sliderMax, int decimals,
                 double defaultValue, const QString& suffix = QString(), QWidget* parent = nullptr);

    double value() const;
    void setValue(double value); // doesn't fire valueChanged
    void reset() { setValue(m_default); emit valueChanged(m_default); }

signals:
    void valueChanged(double value);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int toSlider(double v) const;

    QSlider* m_slider;
    QDoubleSpinBox* m_box;
    double m_scale; // slider steps per unit, e.g. 10 for one decimal place
    double m_default;
    bool m_syncing = false;
};
