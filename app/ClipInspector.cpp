#include "ClipInspector.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QString formatSeconds(double s)
{
    int cs = int(s * 100 + 0.5);
    return QString::asprintf("%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

QDoubleSpinBox* makeFadeBox()
{
    auto* box = new QDoubleSpinBox;
    box->setRange(0.0, 10.0);
    box->setSingleStep(0.25);
    box->setDecimals(2);
    box->setSuffix(" s");
    return box;
}

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

    m_panel = new QWidget;
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_panel);
    outer->addStretch();

    m_name = new QLabel;
    m_name->setWordWrap(true);
    m_name->setStyleSheet("font-weight: bold; color: #2fc6b4;");
    m_info = new QLabel;
    m_info->setStyleSheet("color: #808286;");
    layout->addWidget(m_name);
    layout->addWidget(m_info);

    // ---- Title ----
    m_titleBox = new QGroupBox("Text");
    auto* titleForm = new QFormLayout(m_titleBox);
    m_text = new QPlainTextEdit;
    m_text->setMaximumHeight(70);
    m_textSize = new QSpinBox;
    m_textSize->setRange(2, 40);
    m_textSize->setSuffix(" %");
    m_textSize->setToolTip("Size, as a percentage of the picture's height");
    m_textY = new QSlider(Qt::Horizontal);
    m_textY->setRange(5, 95);
    m_textY->setToolTip("Left = near the top, right = near the bottom");
    m_color = new QPushButton;
    m_background = new QCheckBox("Dark box behind it");
    m_bold = new QCheckBox("Bold");
    titleForm->addRow(m_text);
    titleForm->addRow("Size", m_textSize);
    titleForm->addRow("Height", m_textY);
    titleForm->addRow("Colour", m_color);
    titleForm->addRow(m_background);
    titleForm->addRow(m_bold);
    layout->addWidget(m_titleBox);

    // ---- Sound ----
    m_soundBox = new QGroupBox("Sound");
    auto* soundLayout = new QVBoxLayout(m_soundBox);
    auto* volumeRow = new QHBoxLayout;
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 200);
    m_volumeLabel = new QLabel;
    m_volumeLabel->setMinimumWidth(40);
    volumeRow->addWidget(new QLabel("Volume"));
    volumeRow->addWidget(m_volume, 1);
    volumeRow->addWidget(m_volumeLabel);
    soundLayout->addLayout(volumeRow);
    m_detach = new QPushButton("Detach audio");
    m_detach->setToolTip("Put this clip's sound on its own audio track");
    soundLayout->addWidget(m_detach);
    layout->addWidget(m_soundBox);

    // ---- Fades ----
    auto* fades = new QGroupBox("Fade");
    auto* fadeForm = new QFormLayout(fades);
    m_fadeIn = makeFadeBox();
    m_fadeOut = makeFadeBox();
    fadeForm->addRow("In", m_fadeIn);
    fadeForm->addRow("Out", m_fadeOut);
    auto* fadeHint = new QLabel("Tip: overlap two clips on different video tracks and fade the top one in for a cross-dissolve.");
    fadeHint->setWordWrap(true);
    fadeHint->setStyleSheet("color: #808286; font-size: 11px;");
    fadeForm->addRow(fadeHint);
    layout->addWidget(fades);

    connect(m_volume, &QSlider::valueChanged, this, [this] { apply("volume"); });
    connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, [this] { apply("fadeIn"); });
    connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, [this] { apply("fadeOut"); });
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] { apply("text"); });
    connect(m_textSize, &QSpinBox::valueChanged, this, [this] { apply("textSize"); });
    connect(m_textY, &QSlider::valueChanged, this, [this] { apply("textY"); });
    connect(m_background, &QCheckBox::toggled, this, [this] { apply("textBox"); });
    connect(m_bold, &QCheckBox::toggled, this, [this] { apply("textBold"); });
    connect(m_color, &QPushButton::clicked, this, &ClipInspector::pickColor);
    connect(m_detach, &QPushButton::clicked, this, &ClipInspector::detachAudioClicked);

    showClip(-1, {});
}

void ClipInspector::showClip(int index, const TimelineClip& clip)
{
    m_index = index;
    m_clip = clip;
    m_empty->setVisible(index < 0);
    m_panel->setVisible(index >= 0);
    if (index < 0)
        return;

    m_loading = true;
    m_name->setText(clip.isTitle() ? QStringLiteral("Title") : clip.name);
    m_info->setText(QString("Starts at %1 · %2 long").arg(formatSeconds(clip.start), formatSeconds(clip.duration)));

    m_titleBox->setVisible(clip.isTitle());
    if (clip.isTitle()) {
        if (m_text->toPlainText() != clip.title.text)
            m_text->setPlainText(clip.title.text);
        m_textSize->setValue(clip.title.size);
        m_textY->setValue(int(clip.title.y * 100));
        m_background->setChecked(clip.title.box);
        m_bold->setChecked(clip.title.bold);
        refreshColorButton();
    }

    m_soundBox->setVisible(clip.hasAudio && clip.audioOn);
    m_volume->setValue(int(clip.volume * 100 + 0.5));
    m_volumeLabel->setText(QString("%1%").arg(m_volume->value()));
    m_detach->setVisible(clip.showsVideo() && clip.playsAudio());

    m_fadeIn->setValue(clip.fadeIn);
    m_fadeOut->setValue(clip.fadeOut);
    m_loading = false;
}

void ClipInspector::apply(const QString& what)
{
    if (m_loading || m_index < 0)
        return;

    m_clip.volume = m_volume->value() / 100.0f;
    m_volumeLabel->setText(QString("%1%").arg(m_volume->value()));
    // A fade can't be longer than the clip itself
    m_clip.fadeIn = std::min(m_fadeIn->value(), m_clip.duration);
    m_clip.fadeOut = std::min(m_fadeOut->value(), m_clip.duration);

    if (m_clip.isTitle()) {
        m_clip.title.text = m_text->toPlainText();
        m_clip.title.size = m_textSize->value();
        m_clip.title.y = m_textY->value() / 100.0;
        m_clip.title.box = m_background->isChecked();
        m_clip.title.bold = m_bold->isChecked();
        QString first = m_clip.title.text.section('\n', 0, 0).trimmed();
        m_clip.name = first.isEmpty() ? QStringLiteral("Title") : first;
    }
    emit edited(m_index, m_clip, what);
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
