// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "NumberSlider.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QSlider>

#include <cmath>

NumberSlider::NumberSlider(double min, double max, double sliderMin, double sliderMax, int decimals,
                           double defaultValue, const QString& suffix, QWidget* parent)
    : QWidget(parent)
    , m_scale(std::pow(10.0, decimals))
    , m_default(defaultValue)
{
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setMinimumWidth(60);
    m_slider->setRange(toSlider(sliderMin), toSlider(sliderMax));
    m_slider->setToolTip("Drag to change, double-click to reset");
    m_slider->installEventFilter(this);

    m_box = new QDoubleSpinBox;
    m_box->setRange(min, max);
    m_box->setDecimals(decimals);
    m_box->setSingleStep(decimals > 1 ? 0.05 : 1.0);
    m_box->setSuffix(suffix);
    m_box->setKeyboardTracking(false); // only apply typed numbers on Enter (no flicker while typing "1.")
    m_box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_box->setAlignment(Qt::AlignRight);
    m_box->setFixedWidth(68);
    m_box->setToolTip("Type an exact number");

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_slider, 1);
    row->addWidget(m_box);

    connect(m_slider, &QSlider::valueChanged, this, [this](int step) {
        if (m_syncing)
            return;
        double v = step / m_scale;
        m_syncing = true;
        m_box->setValue(v);
        m_syncing = false;
        emit valueChanged(v);
    });
    connect(m_box, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_syncing)
            return;
        m_syncing = true;
        m_slider->setValue(toSlider(v)); // a typed value past the slider's ends just pins it there
        m_syncing = false;
        emit valueChanged(v);
    });

    setValue(defaultValue);
}

int NumberSlider::toSlider(double v) const
{
    return int(std::lround(v * m_scale));
}

double NumberSlider::value() const
{
    return m_box->value();
}

void NumberSlider::setValue(double value)
{
    m_syncing = true;
    m_box->setValue(value);
    m_slider->setValue(toSlider(value));
    m_syncing = false;
}

bool NumberSlider::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_slider && event->type() == QEvent::MouseButtonDblClick) {
        reset();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
