// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppPaths.h"
#include "ClipInspector.h"
#include "EffectNames.h"
#include "HelpWindow.h"
#include "NumberSlider.h"
#include "Theme.h"

#include <ve/engine.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMouseEvent>
#include <QScrollArea>
#include <array>
#include <QHash>
#include <QImage>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QToolButton>
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
    b->setProperty("role", "link");
    return b;
}

const double SpeedPresets[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0 };

} // namespace

ClipInspector::ClipInspector(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 8, 6, 8);
    setObjectName("card"); // (a rounded panel, like the library's)
    setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel("Properties");
    title->setProperty("role", "heading");
    // "?" opens the help page for whatever's selected
    auto* heading = new QHBoxLayout;
    heading->addWidget(title);
    heading->addStretch();
    heading->addWidget(HelpWindow::button([this] {
        if (m_index < 0)
            return QStringLiteral("clips.md");
        if (m_clip.isSubtitle())
            return QStringLiteral("subtitles.md");
        if (m_clip.isTitle())
            return QStringLiteral("titles.md");
        if (m_clip.isEffect() || m_clip.isTransition())
            return QStringLiteral("effects-and-transitions.md");
        return m_clip.audioOnly() ? QStringLiteral("sound.md") : QStringLiteral("clips.md");
    }));
    outer->addLayout(heading);

    // Nothing picked yet: say what goes here, plus a few handy things worth knowing
    m_empty = new QWidget;
    auto* emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    auto* what = new QLabel("Click a clip on the timeline and everything you can change about it shows up here: "
                            "speed, look, size, words, sound…");
    auto* tips = new QLabel("<p><b>Handy to know</b></p>"
                            "<p>S splits whatever's under the playhead<br>"
                            "Ctrl+Z undoes pretty much anything<br>"
                            "Click a section's title to fold it away<br>"
                            "F1 opens the help, any time</p>");
    tips->setProperty("role", "dim"); // (greyed by the theme, so it follows light and dark)
    for (QLabel* l : { what, tips }) {
        l->setWordWrap(true);
        emptyLayout->addWidget(l);
    }
    emptyLayout->insertSpacing(1, 14); // (a gap between the two)
    emptyLayout->addStretch();
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
    m_name->setProperty("role", "accent");
    m_info = new QLabel;
    m_info->setProperty("role", "dim");
    auto* hint = new QLabel("Drag a slider or type an exact number. Double-click a slider to reset it.");
    hint->setWordWrap(true);
    hint->setProperty("role", "hint");
    layout->addWidget(m_name);
    layout->addWidget(m_info);
    layout->addWidget(hint);

    // ---- Quick zoom: right up top, never folded away ----
    m_quickBox = new QGroupBox("Quick zoom");
    auto* quick = new QVBoxLayout(m_quickBox);
    auto* styles = new QHBoxLayout;
    auto* styleGroup = new QButtonGroup(this);
    const QSettings saved = AppPaths::settings();
    m_zoomStyle = saved.value("zoom/style", "punch").toString();
    const struct {
        const char *id, *text, *tip;
    } kinds[] = {
        { "punch", "Punch", "Snaps straight in. The classic \"wait, WHAT\" zoom." },
        { "bounce", "Bounce", "Zooms in, goes a bit past, and springs back" },
        { "smooth", "Smooth", "Glides in and out nice and calm" },
    };
    for (const auto& k : kinds) {
        auto* b = new QPushButton(k.text);
        b->setCheckable(true);
        b->setChecked(m_zoomStyle == k.id);
        b->setToolTip(QString(k.tip) + ". Adds one at the playhead (Z does it again).");
        b->setProperty("style", k.id);
        styleGroup->addButton(b);
        styles->addWidget(b);
        m_styleButtons << b;
        connect(b, &QPushButton::clicked, this, [this, id = QString(k.id)] {
            m_zoomStyle = id;
            AppPaths::settings().setValue("zoom/style", id);
            zoomHere();
        });
    }
    quick->addLayout(styles);
    auto* extras = new QHBoxLayout;
    m_zoomShake = new QCheckBox("Shake when it lands");
    m_zoomShake->setChecked(saved.value("zoom/shake", false).toBool());
    connect(m_zoomShake, &QCheckBox::toggled, this, [](bool on) { AppPaths::settings().setValue("zoom/shake", on); });
    auto* quickPick = new QPushButton("Pick a spot");
    quickPick->setToolTip("Click where to zoom in on the preview (it zooms into the middle otherwise)");
    connect(quickPick, &QPushButton::clicked, this, &ClipInspector::pickSpotRequested);
    extras->addWidget(m_zoomShake);
    extras->addStretch();
    extras->addWidget(quickPick);
    quick->addLayout(extras);
    m_spotLabel = new QLabel("Zooms in at the playhead, into the middle. Press Z to do it again anywhere.");
    m_spotLabel->setWordWrap(true);
    m_spotLabel->setProperty("role", "hint");
    quick->addWidget(m_spotLabel);
    layout->addWidget(m_quickBox);

    // ---- Title ----
    m_titleBox = new QGroupBox("Text");
    auto* titleForm = new QFormLayout(m_titleBox);
    m_text = new QPlainTextEdit;
    m_text->setMaximumHeight(70);
    titleForm->addRow(m_text);
    m_font = new QFontComboBox;
    titleForm->addRow("Font", m_font);
    m_bold = new QCheckBox("Bold");
    m_italic = new QCheckBox("Italic");
    auto* styleRow = new QHBoxLayout;
    styleRow->addWidget(m_bold);
    styleRow->addWidget(m_italic);
    styleRow->addStretch();
    titleForm->addRow(styleRow);
    m_textSize = addSlider(titleForm, "Size", "textSize", 1, 60, 2, 40, 8, " %");
    m_textSize->setToolTip("Size, as a percentage of the picture's height");
    m_color = colorButton(&TitleStyle::color, "Text colour", false);
    titleForm->addRow("Colour", m_color);
    m_align = new QComboBox;
    m_align->addItem("Left", 0);
    m_align->addItem("Centre", 1);
    m_align->addItem("Right", 2);
    titleForm->addRow("Line up", m_align);
    m_textX = addSlider(titleForm, "Left / right", "textX", 0, 100, 0, 100, 50, " %");
    m_textX->setToolTip("Where the side it's lined up to sits: 0% = left edge of the picture, 100% = right edge");
    m_textY = addSlider(titleForm, "Up / down", "textY", 0, 100, 0, 100, 82, " %");
    m_textY->setToolTip("0% = top of the picture, 100% = bottom");
    m_spacing = addSlider(titleForm, "Letter spacing", "spacing", -50, 300, -50, 150, 0, " %");

    m_outline = addSlider(titleForm, "Outline", "outline", 0, 30, 0, 20, 0, " %");
    m_outline->setToolTip("How thick, compared to the text size");
    m_outlineColor = colorButton(&TitleStyle::outlineColor, "Outline colour", true);
    titleForm->addRow("Outline colour", m_outlineColor);

    m_shadow = new QCheckBox("Shadow");
    m_shadowColor = colorButton(&TitleStyle::shadowColor, "Shadow colour", true);
    auto* shadowRow = new QHBoxLayout;
    shadowRow->addWidget(m_shadow);
    shadowRow->addWidget(m_shadowColor, 1);
    titleForm->addRow(shadowRow);
    m_shadowDistance = addSlider(titleForm, "Shadow distance", "shadowDistance", 0, 50, 0, 30, 6, " %");
    m_shadowDistance->setToolTip("0 with a bright colour makes a glow");
    m_shadowSoftness = addSlider(titleForm, "Shadow softness", "shadowSoftness", 0, 100, 0, 100, 20, " %");

    m_background = new QCheckBox("Box behind it");
    m_boxColor = colorButton(&TitleStyle::boxColor, "Box colour", true);
    auto* boxRow = new QHBoxLayout;
    boxRow->addWidget(m_background);
    boxRow->addWidget(m_boxColor, 1);
    titleForm->addRow(boxRow);

    // Subtitle lines: the look is shared by the whole track unless this line has its own
    m_subtitleRows = new QWidget;
    auto* subForm = new QFormLayout(m_subtitleRows);
    subForm->setContentsMargins(0, 6, 0, 0);
    m_wordByWord = new QCheckBox("Word by word (a few at a time, lit up as they're said)");
    subForm->addRow(m_wordByWord);
    m_highlight = colorButton(&TitleStyle::highlight, "Lit-up word colour", false);
    subForm->addRow("Lit-up colour", m_highlight);
    m_wordsAtOnce = new NumberSlider(1, 12, 1, 8, 0, 3);
    subForm->addRow("Words at once", m_wordsAtOnce);
    m_ownStyle = new QCheckBox("This line has its own look");
    subForm->addRow(m_ownStyle);
    m_lookHint = new QLabel;
    m_lookHint->setWordWrap(true);
    m_lookHint->setProperty("role", "hint");
    subForm->addRow(m_lookHint);
    titleForm->addRow(m_subtitleRows);
    connect(m_wordByWord, &QCheckBox::toggled, this, [this] { apply("wordByWord"); });
    connect(m_ownStyle, &QCheckBox::toggled, this, [this] { apply("ownStyle"); });
    connect(m_wordsAtOnce, &NumberSlider::valueChanged, this, [this] { apply("wordsAtOnce"); });
    layout->addWidget(m_titleBox);
    connect(m_italic, &QCheckBox::toggled, this, [this] { apply("textItalic"); });
    connect(m_shadow, &QCheckBox::toggled, this, [this] { apply("textShadow"); });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this] { apply("textFont"); });
    connect(m_align, &QComboBox::currentIndexChanged, this, [this] { apply("textAlign"); });

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
    m_speed->setToolTip("Below 1× is slow motion, above is fast forward");
    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(m_speed, 1);
    speedRow->addWidget(m_speedPresets, 1);
    speedForm->addRow("Play at", speedRow);
    m_keepPitch = new QCheckBox("Keep the sound's pitch");
    m_keepPitch->setToolTip("On: voices sound normal, just faster or slower.\nOff: like a record played at the wrong speed (chipmunks or giants).");
    speedForm->addRow(m_keepPitch);
    connect(m_keepPitch, &QCheckBox::toggled, this, [this] { apply("keepPitch"); });
    m_reverse = new QCheckBox("Play backwards");
    speedForm->addRow(m_reverse);
    m_freeze = new QPushButton("Freeze frame at the playhead");
    m_freeze->setToolTip("Holds the frame under the playhead for 2 seconds, pushing the rest of the clip along");
    speedForm->addRow(m_freeze);
    connect(m_reverse, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading && m_index >= 0)
            emit reverseChanged(m_index, on);
    });
    connect(m_freeze, &QPushButton::clicked, this, &ClipInspector::freezeFrameClicked);
    layout->addWidget(m_speedBox);

    // ---- Effects ----
    m_effectsBox = new QGroupBox("Effects");
    auto* fxForm = new QFormLayout(m_effectsBox);
    m_look = new QComboBox;
    m_look->setToolTip("Built-in looks, then your own (.cube files in the looks folder)");
    fillLooks();
    fxForm->addRow("Look", m_look);
    m_lutStrength = addSlider(fxForm, "Look strength", "lutStrength", 0, 100, 0, 100, 100, " %");
    m_lutStrength->setToolTip("How much of your look to use: 100% is all of it");
    m_brightness = addSlider(fxForm, "Brightness", "brightness", -100, 100, -100, 100, 0);
    m_contrast = addSlider(fxForm, "Contrast", "contrast", -100, 100, -100, 100, 0);
    m_saturation = addSlider(fxForm, "Colour", "saturation", -100, 100, -100, 100, 0);
    m_temperature = addSlider(fxForm, "Warmth", "temperature", -100, 100, -100, 100, 0);
    m_blur = addSlider(fxForm, "Blur", "blur", 0, 100, 0, 100, 0);
    m_sharpen = addSlider(fxForm, "Sharpen", "sharpen", 0, 100, 0, 100, 0);
    m_vignette = addSlider(fxForm, "Vignette", "vignette", 0, 100, 0, 100, 0);
    m_strength = addSlider(fxForm, "Strength", "strength", 0, 100, 0, 100, 100, " %");
    m_strength->setToolTip("How strongly this effect applies to everything below it");
    QPushButton* resetFx = makeResetButton("Reset effects");
    fxForm->addRow(resetFx);
    layout->addWidget(m_effectsBox);

    // ---- Position & size (picture-in-picture) ----
    m_placeBox = new QGroupBox("Position && size");
    auto* placeForm = new QFormLayout(m_placeBox);
    // The sliders cover the handy range; type a number to go further (e.g. fully off screen)
    // Keyframes: ◆ next to a setting sets its value at the playhead, and it glides from one to the next
    m_keyNav = new QWidget;
    auto* navRow = new QHBoxLayout(m_keyNav);
    navRow->setContentsMargins(0, 0, 0, 0);
    // (only shows up once there are keyframes to jump between)
    auto* keysLabel = new QLabel("Keyframes");
    m_prevKey = new QPushButton("◀");
    m_nextKey = new QPushButton("▶");
    m_clearKeys = makeResetButton("Clear");
    m_prevKey->setToolTip("Jump to the previous keyframe");
    m_nextKey->setToolTip("Jump to the next keyframe");
    m_clearKeys->setToolTip("Remove every keyframe from this clip (the settings stay where they are at the playhead)");
    navRow->addWidget(keysLabel);
    navRow->addWidget(m_prevKey);
    navRow->addWidget(m_nextKey);
    navRow->addSpacing(6);
    navRow->addWidget(new QLabel("Gets there:"));
    m_keyEase = new QComboBox;
    m_keyEase->addItem("Smooth", VE_EASE_SMOOTH);
    m_keyEase->addItem("Straight", VE_EASE_STRAIGHT);
    m_keyEase->addItem("Bounce", VE_EASE_BOUNCE);
    m_keyEase->addItem("Jump", VE_EASE_JUMP);
    m_keyEase->setToolTip("How it gets to the keyframe at the playhead from the one before:\n"
                          "Smooth glides in, Straight goes at one speed, Bounce goes a bit past and springs back,\n"
                          "Jump stays put and then snaps there");
    navRow->addWidget(m_keyEase);
    navRow->addStretch();
    navRow->addWidget(m_clearKeys);
    // (up top with Quick zoom, so it's there whichever sections are folded)
    layout->insertWidget(layout->indexOf(m_quickBox) + 1, m_keyNav);
    // Ready-made spots, for stacking a few videos on top of each other
    struct Spot {
        const char* name;
        double size, x, y;
    };
    static const Spot spots[] = {
        { "Full screen", 1.0, 0, 0 },
        { "Top-left corner", 0.35, -0.295, -0.285 },  { "Top-right corner", 0.35, 0.295, -0.285 },
        { "Bottom-left corner", 0.35, -0.295, 0.285 }, { "Bottom-right corner", 0.35, 0.295, 0.285 },
        { "Left half", 0.5, -0.25, 0 },  { "Right half", 0.5, 0.25, 0 },
        { "Top half", 0.5, 0, -0.25 },   { "Bottom half", 0.5, 0, 0.25 },
        { "Grid: top left", 0.5, -0.25, -0.25 },    { "Grid: top right", 0.5, 0.25, -0.25 },
        { "Grid: bottom left", 0.5, -0.25, 0.25 },  { "Grid: bottom right", 0.5, 0.25, 0.25 },
    };
    auto* quickSpot = new QComboBox;
    quickSpot->addItem("Pick a spot…");
    for (const Spot& s : spots)
        quickSpot->addItem(s.name);
    quickSpot->setToolTip("Puts it in a ready-made spot (with the right size). Put another clip on the track below "
                          "and it shows behind this one.");
    placeForm->addRow("Quick spot", quickSpot);
    connect(quickSpot, &QComboBox::activated, this, [this, quickSpot](int i) {
        quickSpot->setCurrentIndex(0); // it's a shortcut, not a setting
        if (i <= 0 || m_index < 0)
            return;
        const Spot& s = spots[i - 1];
        m_scale->setValue(s.size * 100);
        m_posX->setValue(s.x * 100);
        m_posY->setValue(s.y * 100);
        apply("quickSpot"); // (one undo step for all three)
    });
    m_scale = addKeySlider(placeForm, "Size", "scale", VE_KEY_SIZE, 100, 5, 1000, 10, 200, 100, " %");
    m_posX = addKeySlider(placeForm, "Left / right", "posX", VE_KEY_POS_X, 100, -500, 500, -100, 100, 0, " %");
    m_posY = addKeySlider(placeForm, "Up / down", "posY", VE_KEY_POS_Y, 100, -500, 500, -100, 100, 0, " %");
    m_opacity = addKeySlider(placeForm, "Opacity", "opacity", VE_KEY_OPACITY, 100, 0, 100, 0, 100, 100, " %");
    m_shake = addKeySlider(placeForm, "Shake", "shake", VE_KEY_SHAKE, 100, 0, 100, 0, 100, 0, " %");
    m_shake->setToolTip("Camera shake. Keyframe it for a quick rumble (Zoom & pan below can add one for you).");
    auto* keyHint = new QLabel("◆ adds a keyframe at the playhead. Add two or more and the setting glides between them.");
    keyHint->setWordWrap(true);
    keyHint->setProperty("role", "hint");
    placeForm->addRow(keyHint);
    connect(m_prevKey, &QPushButton::clicked, this, [this] { jumpToKey(-1); });
    connect(m_keyEase, &QComboBox::activated, this, [this] {
        // Every setting's keyframe right here arrives the new way
        const int ease = m_keyEase->currentData().toInt();
        for (QList<Keyframe>& keys : m_clip.keys)
            for (Keyframe& f : keys)
                if (std::abs(f.time - local()) < TimelineClip::KeySnap)
                    f.ease = ease;
        emit edited(m_index, m_clip, "keyEase");
    });
    connect(m_nextKey, &QPushButton::clicked, this, [this] { jumpToKey(1); });
    connect(m_clearKeys, &QPushButton::clicked, this, &ClipInspector::clearKeys);
    QPushButton* resetPlace = makeResetButton("Reset position && size");
    placeForm->addRow(resetPlace);
    layout->addWidget(m_placeBox);

    // ---- Crop & rotate ----
    m_cropBox = new QGroupBox("Crop && rotate");
    auto* cropForm = new QFormLayout(m_cropBox);
    m_fill = new QCheckBox("Fill the frame (no black bars)");
    m_fill->setToolTip("Zooms in just enough to cover the whole picture, cutting off what hangs over the edges.\n"
                       "Handy for putting wide footage in a tall video. Use Left / right above to pick which part shows.");
    cropForm->addRow(m_fill);
    m_blurFill = new QCheckBox("Blurred background (instead of black bars)");
    m_blurFill->setToolTip("The gaps around it show a blurred, zoomed-in copy of the same video, like on phones.\n"
                           "Handy for wide footage in a tall video when you don't want to cut any of it off.");
    cropForm->addRow(m_blurFill);
    m_rotation = addKeySlider(cropForm, "Rotate", "rotation", VE_KEY_ROTATION, 1, -3600, 3600, -180, 180, 0, " °");
    m_rotation->setToolTip("Clockwise. Type a number for an exact angle.");
    auto* turnLeft = new QPushButton("↺ 90°");
    auto* turnRight = new QPushButton("↻ 90°");
    m_flipH = new QPushButton("Mirror ↔");
    m_flipV = new QPushButton("Flip ↕");
    for (QPushButton* b : { m_flipH, m_flipV })
        b->setCheckable(true); // (the theme lights them up when they're on)
    turnLeft->setToolTip("A quarter turn anticlockwise");
    turnRight->setToolTip("A quarter turn clockwise");
    m_flipH->setToolTip("Mirror it left to right");
    m_flipV->setToolTip("Turn it upside down (mirrored)");
    // Two by two, so the panel can stay narrow
    auto* turnGrid = new QGridLayout;
    turnGrid->addWidget(turnLeft, 0, 0);
    turnGrid->addWidget(turnRight, 0, 1);
    turnGrid->addWidget(m_flipH, 1, 0);
    turnGrid->addWidget(m_flipV, 1, 1);
    cropForm->addRow(turnGrid);
    m_cropLeft = addSlider(cropForm, "Crop left", "cropLeft", 0, 95, 0, 95, 0, " %");
    m_cropRight = addSlider(cropForm, "Crop right", "cropRight", 0, 95, 0, 95, 0, " %");
    m_cropTop = addSlider(cropForm, "Crop top", "cropTop", 0, 95, 0, 95, 0, " %");
    m_cropBottom = addSlider(cropForm, "Crop bottom", "cropBottom", 0, 95, 0, 95, 0, " %");
    QPushButton* resetCrop = makeResetButton("Reset crop && rotation");
    cropForm->addRow(resetCrop);
    layout->addWidget(m_cropBox);

    // ---- Frame: shape, border and shadow (made for a facecam over gameplay) ----
    m_frameBox = new QGroupBox("Frame");
    auto* frameForm = new QFormLayout(m_frameBox);
    auto* facecam = new QComboBox;
    facecam->addItem("Pick a facecam look…");
    for (const char* look : { "Circle, bottom right", "Circle, bottom left", "Circle, top right", "Circle, top left",
                              "Rounded box, bottom right", "Rounded box, bottom left", "Rounded box, top right",
                              "Rounded box, top left" })
        facecam->addItem(look);
    facecam->setToolTip("One click: shape, border, shadow, size and spot, ready for a facecam over gameplay");
    frameForm->addRow("Facecam", facecam);
    connect(facecam, &QComboBox::activated, this, [this, facecam](int i) {
        facecam->setCurrentIndex(0); // (a shortcut, not a setting)
        if (i > 0)
            applyFacecam(i - 1);
    });
    m_shape = new QComboBox;
    m_shape->addItem("Rectangle", VE_SHAPE_RECTANGLE);
    m_shape->addItem("Rounded corners", VE_SHAPE_ROUNDED);
    m_shape->addItem("Circle", VE_SHAPE_CIRCLE);
    m_shape->setToolTip("Circle uses the biggest circle that fits, so it also crops the sides off to make it square");
    frameForm->addRow("Shape", m_shape);
    m_corners = addSlider(frameForm, "Corners", "corners", 0, 50, 0, 50, 15, " %");
    m_corners->setToolTip("How round the corners are");
    m_border = addSlider(frameForm, "Border", "border", 0, 10, 0, 10, 0, " %");
    m_border->setToolTip("How thick, compared to the clip's shorter side. 0 = no border.");
    m_borderColor = new QPushButton;
    frameForm->addRow("Border colour", m_borderColor);
    m_frameShadow = addSlider(frameForm, "Shadow", "frameShadow", 0, 100, 0, 100, 0, " %");
    m_frameShadow->setToolTip("A soft shadow behind it, so it stands out from what's underneath");
    layout->addWidget(m_frameBox);
    connect(m_shape, &QComboBox::currentIndexChanged, this, [this] {
        const bool circle = m_shape->currentData().toInt() == VE_SHAPE_CIRCLE;
        if (auto* form = qobject_cast<QFormLayout*>(m_frameBox->layout()))
            form->setRowVisible(m_corners, m_shape->currentData().toInt() == VE_SHAPE_ROUNDED);
        // A circle from a wide picture: crop it square first (if it hasn't been cropped already)
        if (circle && !m_loading && m_cropLeft->value() == 0 && m_cropRight->value() == 0 && m_cropTop->value() == 0
            && m_cropBottom->value() == 0 && m_clip.width > 0 && m_clip.height > 0) {
            const double aspect = double(m_clip.width) / m_clip.height;
            m_loading = true;
            if (aspect > 1) {
                m_cropLeft->setValue((1 - 1 / aspect) / 2 * 100);
                m_cropRight->setValue((1 - 1 / aspect) / 2 * 100);
            } else if (aspect < 1) {
                m_cropTop->setValue((1 - aspect) / 2 * 100);
                m_cropBottom->setValue((1 - aspect) / 2 * 100);
            }
            m_loading = false;
        }
        apply("shape");
    });
    connect(m_borderColor, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_clip.borderColor, this, "Border colour");
        if (!c.isValid())
            return;
        m_clip.borderColor = c;
        refreshBorderColorButton();
        if (m_border->value() == 0) // (picking a colour means you want a border)
            m_border->setValue(3);
        apply("borderColor");
    });
    connect(m_fill, &QCheckBox::toggled, this, [this] {
        m_blurFill->setEnabled(!m_fill->isChecked()); // (filled = no gaps to fill)
        apply("fill");
    });
    connect(m_blurFill, &QCheckBox::toggled, this, [this] { apply("blurFill"); });
    connect(m_flipH, &QPushButton::toggled, this, [this] { apply("flipH"); });
    connect(m_flipV, &QPushButton::toggled, this, [this] { apply("flipV"); });
    // Quarter turns land on the nearest quarter, so 10° then ⟳ gives 90°, not 100°
    auto turnBy = [this](int quarters) {
        double r = std::round(m_rotation->value() / 90.0 + quarters) * 90.0;
        m_rotation->setValue(std::remainder(r, 360.0) == -180.0 ? 180.0 : std::remainder(r, 360.0));
        apply("rotation");
    };
    connect(turnLeft, &QPushButton::clicked, this, [turnBy] { turnBy(-1); });
    connect(turnRight, &QPushButton::clicked, this, [turnBy] { turnBy(1); });
    connect(resetCrop, &QPushButton::clicked, this, &ClipInspector::resetCrop);

    // ---- Zoom & pan: ready-made keyframes for size and position ----
    m_zoomBox = new QGroupBox("Zoom && pan");
    auto* zoomForm = new QFormLayout(m_zoomBox);
    m_zoomPreset = new QComboBox;
    m_zoomPreset->addItem("Zoom into a spot", "spot");
    m_zoomPreset->addItem("Slow zoom in (whole clip)", "in");
    m_zoomPreset->addItem("Slow zoom out (whole clip)", "out");
    m_zoomPreset->addItem("Pan left -> right (whole clip)", "panRight");
    m_zoomPreset->addItem("Pan right -> left (whole clip)", "panLeft");
    zoomForm->addRow("Move", m_zoomPreset);
    m_spotControls = new QWidget;
    auto* spotForm = new QFormLayout(m_spotControls);
    spotForm->setContentsMargins(0, 0, 0, 0);
    auto* pickSpot = new QPushButton("Pick the spot on the preview");
    spotForm->addRow(pickSpot);
    // (plain sliders: picking the move doesn't change the clip until you press Add)
    auto plain = [spotForm](const QString& label, double min, double max, double def, const QString& suffix, int decimals) {
        auto* s = new NumberSlider(min, max, min, max, decimals, def, suffix);
        spotForm->addRow(label, s);
        return s;
    };
    m_spotX = plain("Spot left / right", -50, 50, 0, " %", 1);
    m_spotY = plain("Spot up / down", -50, 50, 0, " %", 1);
    m_zoomAmount = plain("Zoom", 110, 500, 200, " %", 0);
    m_zoomTakes = plain("Zooming takes", 0.1, 5, 0.6, " s", 2);
    m_zoomTakes->setToolTip("(A punch doesn't take any time, it snaps straight in)");
    m_zoomHold = plain("Stay zoomed", 0, 60, 2, " s", 2);
    zoomForm->addRow(m_spotControls);
    auto* addZoom = new QPushButton("Add");
    addZoom->setToolTip("Adds it as keyframes on Size and Left / right / Up / down. \"Zoom into a spot\" starts at the playhead, in the Quick zoom style picked up top.");
    zoomForm->addRow(addZoom);
    auto* zoomHint = new QLabel("The fine-tuning for Quick zoom. A zoom into a spot goes on top of what's there, so you can add loads. The whole-clip moves replace the size and position keyframes.");
    zoomHint->setWordWrap(true);
    zoomHint->setProperty("role", "hint");
    zoomForm->addRow(zoomHint);
    layout->addWidget(m_zoomBox);

    // ---- Green screen ----
    m_keyBox = new QGroupBox("Green screen");
    auto* keyForm = new QFormLayout(m_keyBox);
    m_keyOn = new QCheckBox("Remove a colour");
    m_keyOn->setToolTip("Makes everything close to the colour see-through, so the tracks below show through");
    keyForm->addRow(m_keyOn);
    m_keyColor = new QPushButton;
    auto* pickKey = new QPushButton("Pick");
    pickKey->setToolTip("Click the green (or blue) background in the preview");
    auto* findKey = new QPushButton("Find it");
    findKey->setToolTip("Works out the screen's colour from the frame under the playhead");
    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(m_keyColor, 1);
    keyRow->addWidget(pickKey);
    keyRow->addWidget(findKey);
    // The usual screens, to start from (Find it or Pick get the exact shade)
    m_keyPreset = new QComboBox;
    const QList<QPair<QString, QColor>> screens = {
        { "Green", QColor(0x1e, 0xaf, 0x46) }, { "Blue", QColor(0x19, 0x3c, 0xc8) }, { "Red", QColor(0xc8, 0x1e, 0x1e) },
        { "Purple", QColor(0x96, 0x28, 0xaa) }, { "Yellow", QColor(0xe1, 0xcd, 0x28) }, { "White", QColor(0xeb, 0xeb, 0xeb) },
        { "Black", QColor(0x0e, 0x0e, 0x0e) },
    };
    for (const auto& [name, colour] : screens)
        m_keyPreset->addItem(name, colour);
    m_keyPreset->addItem("Your colour"); // (whatever you picked yourself)
    m_keyPreset->setToolTip("What colour your screen is. White and black screens need even lighting to work well.");
    keyForm->addRow("Screen", m_keyPreset);
    connect(m_keyPreset, &QComboBox::activated, this, [this] {
        const QColor c = m_keyPreset->currentData().value<QColor>();
        if (c.isValid())
            setKeyColor(c);
    });
    keyForm->addRow("Colour", keyRow);
    m_keyStrength = addSlider(keyForm, "Strength", "keyStrength", 0, 100, 0, 100, 40, " %");
    m_keyStrength->setToolTip("How different from the colour something can be and still go");
    m_keySoftness = addSlider(keyForm, "Soft edge", "keySoftness", 0, 100, 0, 100, 20, " %");
    m_keySpill = addSlider(keyForm, "Remove glow", "keySpill", 0, 100, 0, 100, 50, " %");
    m_keySpill->setToolTip("Takes the green (or blue) tint off edges and hair");
    m_keyEdges = addSlider(keyForm, "Clean edges", "keyEdges", 0, 100, 0, 100, 30, " %");
    m_keyEdges->setToolTip("Softens the edge, and (turned up) pulls it in a little, so there's no fringe of screen\n"
                           "colour round it. Turn it down if fine hair goes missing.");
    m_keyMatte = new QCheckBox("Show what's see-through");
    m_keyMatte->setToolTip("Just for tuning: the preview shows what's kept in white and what's gone in black.\n"
                           "Aim for solid white and solid black. Never ends up in the export.");
    keyForm->addRow(m_keyMatte);
    layout->addWidget(m_keyBox);
    connect(m_keyOn, &QCheckBox::toggled, this, [this] { apply("keyOn"); });
    connect(m_keyColor, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_clip.keyColor, this, "Colour to remove");
        if (c.isValid())
            setKeyColor(c);
    });
    connect(pickKey, &QPushButton::clicked, this, &ClipInspector::pickColorRequested);
    connect(findKey, &QPushButton::clicked, this, &ClipInspector::findScreenColour);
    connect(m_keyMatte, &QCheckBox::toggled, this, [this] { apply("keyMatte"); });
    connect(m_zoomPreset, &QComboBox::currentIndexChanged, this, [this] {
        m_spotControls->setVisible(m_zoomPreset->currentData() == "spot");
    });
    connect(pickSpot, &QPushButton::clicked, this, &ClipInspector::pickSpotRequested);
    connect(addZoom, &QPushButton::clicked, this, &ClipInspector::applyZoomPan);

    // ---- Sound ----
    m_soundBox = new QGroupBox("Sound");
    auto* soundForm = new QFormLayout(m_soundBox);
    m_volume = addKeySlider(soundForm, "Volume", "volume", VE_KEY_VOLUME, 100, 0, 400, 0, 200, 100, " %");
    m_evenOut = new QPushButton("Even out volume");
    m_evenOut->setToolTip("Listens to the clip and sets the volume so it's nicely loud, without clipping");
    soundForm->addRow(m_evenOut);
    m_denoise = addSlider(soundForm, "Remove noise", "denoise", 0, 100, 0, 100, 0, " %");
    m_denoise->setToolTip("Takes out steady background noise like hum, hiss, fans and air conditioning.\n"
                          "Start around 50% and go up until the noise is gone but voices still sound natural.");
    m_duck = new QCheckBox("Turn down while there's talking");
    m_duck->setToolTip("For music: it turns itself down while there's talking on other clips, and back up after");
    soundForm->addRow(m_duck);
    m_duckAmount = addSlider(soundForm, "How much", "duckAmount", 0, 100, 0, 100, 70, " %");
    connect(m_duck, &QCheckBox::toggled, this, [this] { apply("duck"); });
    connect(m_evenOut, &QPushButton::clicked, this, [this] {
        if (m_index >= 0)
            emit evenOutVolumeClicked(m_index);
    });
    m_detach = new QPushButton("Detach audio");
    m_detach->setToolTip("Put this clip's sound on its own audio track");
    soundForm->addRow(m_detach);
    layout->addWidget(m_soundBox);

    // ---- Transition into this clip ----
    m_transitionBox = new QGroupBox("Transition");
    auto* trForm = new QFormLayout(m_transitionBox);
    m_transition = new QComboBox;
    for (int i = 0; i < VE_TRANSITION_COUNT; ++i)
        m_transition->addItem(transitionName(i), i);
    trForm->addRow("Type", m_transition);
    m_transitionDuration = addSlider(trForm, "Length", "transitionDuration", 0.1, 10, 0.1, 3, 1.0, " s", 2);
    m_transitionHint = new QLabel;
    m_transitionHint->setWordWrap(true);
    m_transitionHint->setProperty("role", "hint");
    trForm->addRow(m_transitionHint);
    layout->addWidget(m_transitionBox);

    // ---- Animation ----
    m_animBox = new QGroupBox("Animation");
    auto* animForm = new QFormLayout(m_animBox);
    m_animIn = new QComboBox;
    m_animOut = new QComboBox;
    for (int i = 0; i < VE_ANIM_COUNT; ++i) {
        m_animIn->addItem(animationName(i), i);
        // Leaving uses the same moves, just backwards ('slide from the left' leaves to the left)
        m_animOut->addItem(QString(animationName(i)).replace("from the", "to the"), i);
    }
    animForm->addRow("Coming in", m_animIn);
    m_animInDuration = addSlider(animForm, "Takes", "animInDuration", 0.05, 10, 0.05, 3, 0.5, " s", 2);
    animForm->addRow("Going out", m_animOut);
    m_animOutDuration = addSlider(animForm, "Takes", "animOutDuration", 0.05, 10, 0.05, 3, 0.5, " s", 2);
    layout->addWidget(m_animBox);

    // ---- Fades ----
    m_fadeBox = new QGroupBox("Fade");
    auto* fadeForm = new QFormLayout(m_fadeBox);
    m_fadeIn = makeSecondsBox(60);
    m_fadeOut = makeSecondsBox(60);
    fadeForm->addRow("In", m_fadeIn);
    fadeForm->addRow("Out", m_fadeOut);
    auto* fadeHint = new QLabel("Tip: overlap two clips on different video tracks and fade the top one in for a cross-dissolve.");
    fadeHint->setWordWrap(true);
    fadeHint->setProperty("role", "hint");
    fadeForm->addRow(fadeHint);
    layout->addWidget(m_fadeBox);
    layout->addStretch();

    connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, [this] { apply("fadeIn"); });
    connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, [this] { apply("fadeOut"); });
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] { apply("text"); });
    connect(m_background, &QCheckBox::toggled, this, [this] { apply("textBox"); });
    connect(m_bold, &QCheckBox::toggled, this, [this] { apply("textBold"); });
    connect(m_detach, &QPushButton::clicked, this, &ClipInspector::detachAudioClicked);
    connect(m_look, &QComboBox::currentIndexChanged, this, [this] {
        if (m_look->currentData() == "folder") {
            // Not a look: open the folder, and put the list back how it was
            AppPaths::openFolder(this, AppPaths::looks());
            QSignalBlocker hush(m_look);
            fillLooks(m_clip.lutPath, m_clip.look);
            return;
        }
        if (auto* form = qobject_cast<QFormLayout*>(m_effectsBox->layout()))
            form->setRowVisible(m_lutStrength, m_look->currentData().toString().startsWith("lut:"));
        apply("look");
    });
    connect(m_transition, &QComboBox::currentIndexChanged, this, [this] { apply("transition"); });
    connect(m_animIn, &QComboBox::currentIndexChanged, this, [this] { apply("animIn"); });
    connect(m_animOut, &QComboBox::currentIndexChanged, this, [this] { apply("animOut"); });
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

    // Sections: click a title to fold it away. The ones most people only need now and then
    // start folded, so the panel isn't a mile long.
    const QList<QPair<QGroupBox*, QString>> sections = {
        { m_titleBox, "text" }, { m_speedBox, "speed" }, { m_effectsBox, "effects" }, { m_placeBox, "position" },
        { m_cropBox, "crop" }, { m_frameBox, "frame" }, { m_zoomBox, "zoom" }, { m_keyBox, "greenscreen" }, { m_soundBox, "sound" },
        { m_transitionBox, "transition" }, { m_animBox, "animation" }, { m_fadeBox, "fade" },
    };
    for (const auto& [box, name] : sections) {
        box->setObjectName(name);
        box->setProperty("heading", box->title());
        box->installEventFilter(this);
        m_sections << box;
    }
    m_folded = AppPaths::settings()
                   .value("properties/folded", QStringList { "crop", "zoom", "greenscreen", "animation" })
                   .toStringList();

    // Narrow panel or big text: everything squeezes in rather than getting cut off at the edge.
    // Rows put their control under the label when there's no room beside it, and drop-downs
    // shrink instead of insisting on fitting their longest choice.
    for (QFormLayout* form : m_panel->findChildren<QFormLayout*>())
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (QComboBox* box : m_panel->findChildren<QComboBox*>()) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(6);
    }

    showClip(-1, {});
}

NumberSlider* ClipInspector::addKeySlider(QFormLayout* form, const QString& label, const QString& what, int param,
                                          double unit, double min, double max, double sliderMin, double sliderMax,
                                          double defaultValue, const QString& suffix)
{
    auto* s = new NumberSlider(min, max, sliderMin, sliderMax, 1, defaultValue, suffix);
    auto* key = new QToolButton;
    key->setText("◇");
    key->setAutoRaise(true);
    key->setFocusPolicy(Qt::NoFocus);
    auto* row = new QWidget;
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    rowLayout->addWidget(s, 1);
    rowLayout->addWidget(key);
    form->addRow(label, row);
    connect(s, &NumberSlider::valueChanged, this, [this, what] { apply(what); });
    connect(key, &QToolButton::clicked, this, [this, param] { toggleKey(param); });
    m_keyControls << KeyControl { param, what, s, key, unit };
    return s;
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

void ClipInspector::showClip(int index, const TimelineClip& clip, int part)
{
    m_index = index;
    m_clip = clip;
    m_part = part;
    m_empty->setVisible(index < 0);
    m_scroll->setVisible(index >= 0);
    if (index < 0)
        return;

    m_loading = true;
    m_name->setText(clip.isTitle()      ? QStringLiteral("Title")
                    : clip.isSubtitle() ? QStringLiteral("Subtitle line")
                    : clip.isEffect()   ? "Effect: " + clip.name
                                        : clip.name);
    QString info = QString("Starts at %1 · %2 long").arg(formatSeconds(clip.start), formatSeconds(clip.duration));
    if (clip.speed != 1.0)
        info += QString(" · %1×").arg(clip.speed);
    m_info->setText(info);

    m_titleBox->setVisible(clip.isTitle() || clip.isSubtitle());
    m_titleBox->setProperty("heading", clip.isSubtitle() ? "Subtitle" : "Text");
    m_subtitleRows->setVisible(clip.isSubtitle());
    if (clip.isSubtitle()) {
        m_ownStyle->setChecked(clip.ownStyle);
        m_wordByWord->setChecked(clip.title.wordByWord);
        m_wordsAtOnce->setValue(clip.title.wordsAtOnce);
        m_wordsAtOnce->setEnabled(clip.title.wordByWord);
        m_highlight->setEnabled(clip.title.wordByWord);
        m_lookHint->setText(clip.ownStyle ? "Look changes only affect this line."
                                          : "Look changes (font, colours, word by word...) apply to every line on this track.");
    }
    if (clip.isTitle() || clip.isSubtitle()) {
        if (m_text->toPlainText() != clip.title.text)
            m_text->setPlainText(clip.title.text);
        m_textSize->setValue(clip.title.size);
        m_textY->setValue(clip.title.y * 100);
        m_textX->setValue(clip.title.x * 100);
        m_background->setChecked(clip.title.box);
        m_bold->setChecked(clip.title.bold);
        m_italic->setChecked(clip.title.italic);
        m_font->setCurrentFont(clip.title.font.isEmpty() ? QFont() : QFont(clip.title.font));
        m_align->setCurrentIndex(std::clamp(clip.title.align, 0, 2));
        m_spacing->setValue(clip.title.spacing);
        m_outline->setValue(clip.title.outline);
        m_shadow->setChecked(clip.title.shadow);
        m_shadowDistance->setValue(clip.title.shadowDistance);
        m_shadowSoftness->setValue(clip.title.shadowSoftness);
        refreshColorButton();
    }

    // Only show what makes sense for this kind of clip
    bool footage = clip.kind == TimelineClip::Kind::Media;
    bool video = footage && clip.showsVideo();
    m_speedBox->setVisible(footage && !clip.isStill());
    m_keyBox->setVisible(video);
    m_freeze->setVisible(video);
    m_effectsBox->setVisible(video || clip.isEffect());
    // (the whole row, label too: just hiding the slider would leave "Strength" on its own)
    if (auto* form = qobject_cast<QFormLayout*>(m_effectsBox->layout()))
        form->setRowVisible(m_strength, clip.isEffect());
    m_placeBox->setVisible(clip.showsVideo());
    m_cropBox->setVisible(clip.showsVideo());
    m_frameBox->setVisible(clip.showsVideo() && clip.kind == TimelineClip::Kind::Media);
    m_shape->setCurrentIndex(std::max(0, m_shape->findData(clip.shape)));
    if (auto* form = qobject_cast<QFormLayout*>(m_frameBox->layout()))
        form->setRowVisible(m_corners, clip.shape == VE_SHAPE_ROUNDED);
    m_corners->setValue(clip.cornerRadius * 100);
    m_border->setValue(clip.borderWidth * 100);
    m_frameShadow->setValue(clip.shadow * 100);
    refreshBorderColorButton();
    m_zoomBox->setVisible(clip.showsVideo());
    m_quickBox->setVisible(clip.showsVideo() && !clip.isEffect());
    m_soundBox->setVisible(clip.hasAudio && clip.audioOn);
    m_transitionBox->setVisible(clip.isTransition());
    m_fadeBox->setVisible(!clip.isTransition() && !clip.isSubtitle());
    m_detach->setVisible(clip.showsVideo() && clip.playsAudio());

    m_speed->setValue(clip.speed);
    m_reverse->setChecked(clip.reverse);
    m_keepPitch->setChecked(clip.keepPitch);
    m_denoise->setValue(clip.denoise * 100);
    m_duck->setChecked(clip.duck);
    m_duckAmount->setValue(clip.duckAmount * 100);
    m_duckAmount->setEnabled(clip.duck);
    m_keyOn->setChecked(clip.chromaKey);
    m_keyStrength->setValue(clip.keyStrength * 100);
    m_keySoftness->setValue(clip.keySoftness * 100);
    m_keySpill->setValue(clip.keySpill * 100);
    m_keyEdges->setValue(clip.keyEdges * 100);
    m_keyMatte->setChecked(clip.keyMatte);
    refreshKeyColorButton();
    fillLooks(clip.lutPath, clip.look);
    m_lutStrength->setValue(clip.lutStrength * 100);
    if (auto* form = qobject_cast<QFormLayout*>(m_effectsBox->layout()))
        form->setRowVisible(m_lutStrength, !clip.lutPath.isEmpty());
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
    m_shake->setValue(clip.shake * 100);
    m_fill->setChecked(clip.fill);
    m_blurFill->setChecked(clip.blurFill);
    m_blurFill->setEnabled(!clip.fill);
    m_rotation->setValue(clip.rotation);
    m_flipH->setChecked(clip.flipH);
    m_flipV->setChecked(clip.flipV);
    m_cropLeft->setValue(clip.cropLeft * 100);
    m_cropRight->setValue(clip.cropRight * 100);
    m_cropTop->setValue(clip.cropTop * 100);
    m_cropBottom->setValue(clip.cropBottom * 100);
    m_volume->setValue(clip.volume * 100);
    m_transition->setCurrentIndex(std::clamp(clip.transition, 0, VE_TRANSITION_COUNT - 1));
    m_transitionDuration->setValue(clip.isTransition() ? clip.duration : clip.transitionDuration);
    m_transitionHint->setText(part == VE_PART_IN    ? "At the start of a clip: brings everything below it in from black."
                              : part == VE_PART_OUT ? "At the end of a clip: takes everything below it out to black."
                              : part == VE_PART_THROUGH
                                  ? "Plays on the spot: everything below it goes out and comes straight back in. "
                                    "Line it up with the start or end of a clip to bring it in or out instead."
                                  : "On a cut: the clip after overlaps the one before by this long, and they blend across it.");
    m_strength->setValue(clip.opacity * 100);
    m_animIn->setCurrentIndex(std::clamp(clip.animIn, 0, VE_ANIM_COUNT - 1));
    m_animOut->setCurrentIndex(std::clamp(clip.animOut, 0, VE_ANIM_COUNT - 1));
    m_animInDuration->setValue(clip.animInDuration);
    m_animOutDuration->setValue(clip.animOutDuration);
    m_animBox->setVisible(clip.showsVideo() && !clip.isEffect());

    m_fadeIn->setValue(clip.fadeIn);
    m_fadeOut->setValue(clip.fadeOut);
    m_loading = false;
    refreshKeys();

    foldSections();

    // Never narrower than what's showing needs (so nothing gets cut off at the edge, whatever
    // the text size); the window makes room by taking it from the other panels
    m_panel->layout()->activate();
    m_scroll->setMinimumWidth(m_panel->minimumSizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 4);
}

void ClipInspector::setPlayhead(double sec)
{
    m_playhead = sec;
    if (m_index >= 0)
        refreshKeys();
}

void ClipInspector::refreshKeys()
{
    m_loading = true;
    const bool on = playheadOnClip();
    const double t = std::clamp(local(), 0.0, m_clip.duration);
    bool any = false;
    for (const KeyControl& k : m_keyControls) {
        const QList<Keyframe>& keys = m_clip.keys[size_t(k.param)];
        bool here = std::any_of(keys.begin(), keys.end(), [&](const Keyframe& f) {
            return std::abs(f.time - local()) < TimelineClip::KeySnap;
        });
        any |= !keys.isEmpty();
        if (!keys.isEmpty())
            k.slider->setValue(m_clip.valueAt(k.param, t) * k.unit); // what it is right now
        // ◆ = a keyframe right here, ◇ = none here; teal = this setting has keyframes
        k.button->setText(here ? "◆" : "◇");
        k.button->setStyleSheet(keys.isEmpty() ? "color: #808286;" : "color: #2fc6b4;");
        k.button->setEnabled(on);
        k.button->setToolTip(!on ? "Move the playhead onto this clip to add keyframes"
                             : here ? "Remove the keyframe at the playhead"
                                    : "Add a keyframe at the playhead");
        // Keyframed settings can only be changed where a keyframe can go
        k.slider->setEnabled(on || keys.isEmpty());
    }
    m_prevKey->setEnabled(any);
    m_nextKey->setEnabled(any);
    // How it arrives at the keyframe(s) right here
    int easeHere = -1;
    for (const QList<Keyframe>& keys : m_clip.keys)
        for (const Keyframe& f : keys)
            if (easeHere < 0 && std::abs(f.time - local()) < TimelineClip::KeySnap)
                easeHere = f.ease;
    m_keyEase->setEnabled(easeHere >= 0);
    m_keyEase->setCurrentIndex(std::max(0, m_keyEase->findData(std::max(easeHere, 0))));
    if (easeHere < 0)
        m_keyEase->setToolTip("Move the playhead onto a keyframe to pick how it gets there");
    else
        m_keyEase->setToolTip("How it gets to the keyframe at the playhead from the one before:\n"
                              "Smooth glides in, Straight goes at one speed, Bounce goes a bit past and springs back,\n"
                              "Jump stays put and then snaps there");
    m_clearKeys->setEnabled(any);
    m_keyNav->setVisible(any && (m_clip.showsVideo() || m_clip.playsAudio()));
    m_loading = false;
}

void ClipInspector::toggleKey(int param)
{
    if (m_index < 0 || !playheadOnClip())
        return;
    const KeyControl* k = nullptr;
    for (const KeyControl& c : m_keyControls)
        if (c.param == param)
            k = &c;
    if (!k)
        return;
    QList<Keyframe>& keys = m_clip.keys[size_t(param)];
    auto here = std::find_if(keys.begin(), keys.end(), [&](const Keyframe& f) {
        return std::abs(f.time - local()) < TimelineClip::KeySnap;
    });
    if (here != keys.end()) {
        keys.erase(here);
        if (keys.isEmpty()) // last one gone: stay put at whatever it was showing
            m_clip.setBaseValue(param, float(k->slider->value() / k->unit));
    } else {
        m_clip.setKey(param, std::clamp(local(), 0.0, m_clip.duration), float(k->slider->value() / k->unit));
    }
    emit edited(m_index, m_clip, "key");
}

void ClipInspector::jumpToKey(int direction)
{
    // The nearest keyframe of any setting, before or after the playhead
    double best = direction > 0 ? 1e18 : -1e18;
    for (const QList<Keyframe>& keys : m_clip.keys)
        for (const Keyframe& f : keys) {
            double at = m_clip.start + f.time;
            if (direction > 0 && at > m_playhead + 1e-3 && at < best)
                best = at;
            if (direction < 0 && at < m_playhead - 1e-3 && at > best)
                best = at;
        }
    if (std::abs(best) < 1e17)
        emit seekRequested(best);
}

void ClipInspector::clearKeys()
{
    // Everything stays how it looks right now, just without the movement
    const double t = std::clamp(local(), 0.0, m_clip.duration);
    for (int p = 0; p < VE_KEY_COUNT; ++p) {
        if (!m_clip.keys[size_t(p)].isEmpty())
            m_clip.setBaseValue(p, m_clip.valueAt(p, t));
        m_clip.keys[size_t(p)].clear();
    }
    emit edited(m_index, m_clip, "clearKeys");
}

void ClipInspector::setSpot(QPointF spot)
{
    m_spotX->setValue(std::clamp(spot.x(), -0.5, 0.5) * 100);
    m_spotY->setValue(std::clamp(spot.y(), -0.5, 0.5) * 100);
    m_zoomPreset->setCurrentIndex(0);
    m_spotLabel->setText(std::abs(spot.x()) < 0.02 && std::abs(spot.y()) < 0.02
                             ? QString("Zooms in at the playhead, into the middle. Press Z to do it again anywhere.")
                             : QString("Zooms in at the playhead, into the spot you picked. Press Z to do it again anywhere."));
}

void ClipInspector::applyZoomPan()
{
    if (m_index < 0)
        return;
    const double d = m_clip.duration;
    auto key = [this, d](int p, double t, double v, int ease = VE_EASE_SMOOTH) {
        m_clip.setKey(p, std::clamp(t, 0.0, d), float(v), ease);
    };

    const QString move = m_zoomPreset->currentData().toString();
    if (move != "spot")
        for (int p : { VE_KEY_SIZE, VE_KEY_POS_X, VE_KEY_POS_Y })
            m_clip.keys[size_t(p)].clear();
    if (move == "in" || move == "out") {
        bool in = move == "in";
        key(VE_KEY_SIZE, 0, in ? 1.0 : 1.25);
        key(VE_KEY_SIZE, d, in ? 1.25 : 1.0);
    } else if (move == "panRight" || move == "panLeft") {
        // Zoomed in a bit so there's room to move, then slide exactly edge to edge
        double edge = (1.3 - 1.0) / 2, dir = move == "panRight" ? 1 : -1;
        key(VE_KEY_SIZE, 0, 1.3);
        key(VE_KEY_SIZE, d, 1.3);
        key(VE_KEY_POS_X, 0, edge * dir);
        key(VE_KEY_POS_X, d, -edge * dir);
    } else {
        // In to the spot, stay a moment, back out. The spot ends up in the middle of the picture
        // (as far as it can go without showing past the edge of the video).
        double zoom = m_zoomAmount->value() / 100, takes = m_zoomTakes->value(), hold = m_zoomHold->value();
        double room = (zoom - 1) / 2;
        double x = std::clamp(-m_spotX->value() / 100 * zoom, -room, room);
        double y = std::clamp(-m_spotY->value() / 100 * zoom, -room, room);
        double t0 = std::clamp(local(), 0.0, d);
        // Punch = straight in and straight back out (a frame each way). Bounce = it overshoots on the way in.
        const QString style = m_zoomStyle;
        const bool punch = style == "punch";
        if (punch)
            takes = 1.0 / 30;
        const int in = punch ? VE_EASE_JUMP : style == "bounce" ? VE_EASE_BOUNCE : VE_EASE_SMOOTH;
        const int out = punch ? VE_EASE_JUMP : VE_EASE_SMOOTH;
        double times[] = { t0, t0 + takes, t0 + takes + hold, t0 + 2 * takes + hold };
        double sizes[] = { 1, zoom, zoom, 1 };
        int eases[] = { VE_EASE_SMOOTH, in, VE_EASE_SMOOTH, out };
        // Goes on top of what's there: only the keyframes in the way go
        const double last = std::min(times[3], d);
        for (int p : { VE_KEY_SIZE, VE_KEY_POS_X, VE_KEY_POS_Y })
            m_clip.keys[size_t(p)].removeIf([&](const Keyframe& f) { return f.time > t0 - 1e-6 && f.time < last + 1e-6; });
        for (int i = 0; i < 4; ++i) {
            if (i > 0 && times[i] > d + 1e-6)
                break; // the clip ends first
            key(VE_KEY_SIZE, times[i], sizes[i], eases[i]);
            key(VE_KEY_POS_X, times[i], sizes[i] > 1 ? x : 0, eases[i]);
            key(VE_KEY_POS_Y, times[i], sizes[i] > 1 ? y : 0, eases[i]);
        }
        // A quick rumble the moment it lands, dying away
        if (m_zoomShake->isChecked()) {
            const double land = times[1];
            m_clip.keys[VE_KEY_SHAKE].removeIf([&](const Keyframe& f) { return f.time > t0 - 1e-6 && f.time < land + 0.5; });
            key(VE_KEY_SHAKE, t0, 0);
            key(VE_KEY_SHAKE, land, 0.8, VE_EASE_JUMP);
            key(VE_KEY_SHAKE, land + 0.45, 0);
        }
    }
    emit edited(m_index, m_clip, "zoomPan");
}

void ClipInspector::zoomHere()
{
    if (m_index < 0)
        return;
    if (!playheadOnClip()) {
        m_spotLabel->setText("Move the playhead onto this clip first, then zoom away!");
        return;
    }
    for (QPushButton* b : m_styleButtons) // (shows which one Z does)
        b->setChecked(b->property("style") == m_zoomStyle);
    m_zoomPreset->setCurrentIndex(m_zoomPreset->findData("spot"));
    applyZoomPan();
}

void ClipInspector::apply(const QString& what)
{
    if (m_loading || m_index < 0)
        return;

    float before[VE_KEY_COUNT];
    for (int p = 0; p < VE_KEY_COUNT; ++p)
        before[p] = m_clip.baseValue(p);
    m_clip.volume = float(m_volume->value() / 100);
    // A fade can't be longer than the clip itself
    m_clip.fadeIn = std::min(m_fadeIn->value(), m_clip.duration);
    m_clip.fadeOut = std::min(m_fadeOut->value(), m_clip.duration);

    // A built-in look is a number; one of yours is "lut:" and its file
    const QVariant pick = m_look->currentData();
    if (pick.toString().startsWith("lut:")) {
        m_clip.look = 0;
        m_clip.lutPath = pick.toString().mid(4);
    } else {
        m_clip.look = pick.toInt();
        m_clip.lutPath.clear();
    }
    m_clip.lutStrength = float(m_lutStrength->value() / 100);
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
    m_clip.opacity = float((m_clip.isEffect() ? m_strength->value() : m_opacity->value()) / 100);
    m_clip.shake = float(m_shake->value() / 100);

    // Opposite crops can't eat the whole picture: whichever you just moved stops where the other one is
    auto keepSome = [&](NumberSlider* moved, NumberSlider* other) {
        if (moved->value() + other->value() > 95)
            moved->setValue(95 - other->value());
    };
    if (what == "cropLeft") keepSome(m_cropLeft, m_cropRight);
    if (what == "cropRight") keepSome(m_cropRight, m_cropLeft);
    if (what == "cropTop") keepSome(m_cropTop, m_cropBottom);
    if (what == "cropBottom") keepSome(m_cropBottom, m_cropTop);
    m_clip.keepPitch = m_keepPitch->isChecked();
    m_clip.denoise = float(m_denoise->value() / 100);
    m_clip.duck = m_duck->isChecked();
    m_clip.duckAmount = float(m_duckAmount->value() / 100);
    m_clip.chromaKey = m_keyOn->isChecked();
    m_clip.keyStrength = float(m_keyStrength->value() / 100);
    m_clip.keySoftness = float(m_keySoftness->value() / 100);
    m_clip.keySpill = float(m_keySpill->value() / 100);
    m_clip.keyEdges = float(m_keyEdges->value() / 100);
    m_clip.keyMatte = m_keyMatte->isChecked();
    m_clip.fill = m_fill->isChecked();
    m_clip.blurFill = m_blurFill->isChecked();
    m_clip.shape = m_shape->currentData().toInt();
    m_clip.cornerRadius = float(m_corners->value() / 100);
    m_clip.borderWidth = float(m_border->value() / 100);
    m_clip.shadow = float(m_frameShadow->value() / 100);
    m_clip.rotation = float(m_rotation->value());
    m_clip.flipH = m_flipH->isChecked();
    m_clip.flipV = m_flipV->isChecked();
    m_clip.cropLeft = float(m_cropLeft->value() / 100);
    m_clip.cropRight = float(m_cropRight->value() / 100);
    m_clip.cropTop = float(m_cropTop->value() / 100);
    m_clip.cropBottom = float(m_cropBottom->value() / 100);

    if (m_clip.isTransition()) {
        // The timeline slides the clips to fit the new length
        m_clip.transition = m_transition->currentData().toInt();
        m_clip.name = transitionName(m_clip.transition);
        m_clip.duration = m_transitionDuration->value();
    }
    m_clip.animIn = m_animIn->currentData().toInt();
    m_clip.animOut = m_animOut->currentData().toInt();
    // Coming in and going out can't overlap
    m_clip.animInDuration = std::min(m_animInDuration->value(), m_clip.duration);
    m_clip.animOutDuration = std::min(m_animOutDuration->value(), m_clip.duration);

    if (m_clip.isSubtitle()) {
        m_clip.ownStyle = m_ownStyle->isChecked();
        m_clip.title.wordByWord = m_wordByWord->isChecked();
        m_clip.title.wordsAtOnce = int(m_wordsAtOnce->value());
    }
    if (m_clip.isTitle() || m_clip.isSubtitle()) {
        m_clip.title.text = m_text->toPlainText();
        m_clip.title.size = m_textSize->value();
        m_clip.title.y = m_textY->value() / 100;
        m_clip.title.box = m_background->isChecked();
        m_clip.title.bold = m_bold->isChecked();
        m_clip.title.italic = m_italic->isChecked();
        // (the usual font stays "the usual one", so the project looks right on another computer)
        QString family = m_font->currentFont().family();
        m_clip.title.font = family == QFont().family() ? QString() : family;
        m_clip.title.align = m_align->currentData().toInt();
        m_clip.title.x = m_textX->value() / 100;
        m_clip.title.spacing = m_spacing->value();
        m_clip.title.outline = m_outline->value();
        m_clip.title.shadow = m_shadow->isChecked();
        m_clip.title.shadowDistance = m_shadowDistance->value();
        m_clip.title.shadowSoftness = m_shadowSoftness->value();
        QString first = m_clip.title.text.section('\n', 0, 0).trimmed();
        m_clip.name = first.isEmpty() ? QString(m_clip.isSubtitle() ? "Subtitle" : "Title") : first;
    }
    // Keyframed settings: the slider shows (and edits) the keyframe at the playhead instead
    for (const KeyControl& k : m_keyControls) {
        if (m_clip.keys[size_t(k.param)].isEmpty())
            continue;
        m_clip.setBaseValue(k.param, before[k.param]);
        if (what == k.what && playheadOnClip())
            m_clip.setKey(k.param, std::clamp(local(), 0.0, m_clip.duration), float(k.slider->value() / k.unit));
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
    for (int p : { VE_KEY_SIZE, VE_KEY_POS_X, VE_KEY_POS_Y, VE_KEY_OPACITY, VE_KEY_SHAKE })
        m_clip.keys[size_t(p)].clear();
    m_loading = true;
    m_scale->setValue(100);
    m_posX->setValue(0);
    m_posY->setValue(0);
    m_opacity->setValue(100);
    m_shake->setValue(0);
    m_loading = false;
    apply("resetPlacement");
}

void ClipInspector::resetCrop()
{
    m_clip.keys[VE_KEY_ROTATION].clear();
    m_loading = true;
    m_fill->setChecked(false);
    m_blurFill->setChecked(false);
    m_rotation->setValue(0);
    m_flipH->setChecked(false);
    m_flipV->setChecked(false);
    for (NumberSlider* s : { m_cropLeft, m_cropRight, m_cropTop, m_cropBottom })
        s->setValue(0);
    m_loading = false;
    apply("resetCrop");
}

QPushButton* ClipInspector::colorButton(QColor TitleStyle::*which, const QString& title, bool seeThrough)
{
    auto* b = new QPushButton;
    connect(b, &QPushButton::clicked, this, [=, this] {
        QColorDialog::ColorDialogOptions options;
        if (seeThrough)
            options |= QColorDialog::ShowAlphaChannel;
        QColor c = QColorDialog::getColor(m_clip.title.*which, this, title, options);
        if (!c.isValid())
            return;
        m_clip.title.*which = c;
        refreshColorButton();
        apply("textColor");
    });
    return b;
}

void ClipInspector::setKeyColor(const QColor& color)
{
    if (m_index < 0 || !color.isValid())
        return;
    m_clip.keyColor = color;
    refreshKeyColorButton();
    if (!m_keyOn->isChecked()) {
        m_loading = true;
        m_keyOn->setChecked(true); // picking a colour means you want it gone
        m_loading = false;
    }
    apply("keyColor");
}

void ClipInspector::refreshKeyColorButton()
{
    if (m_keyPreset) {
        // One of the usual screens, or your own colour
        QSignalBlocker hush(m_keyPreset);
        const int preset = m_keyPreset->findData(m_clip.keyColor);
        m_keyPreset->setCurrentIndex(preset >= 0 ? preset : m_keyPreset->count() - 1);
    }
    m_keyColor->setText(m_clip.keyColor.name());
    m_keyColor->setStyleSheet(QString("QPushButton { background: %1; color: %2; }")
                                  .arg(m_clip.keyColor.name(), m_clip.keyColor.lightness() > 128 ? "black" : "white"));
}

void ClipInspector::refreshColorButton()
{
    auto show = [](QPushButton* b, const QColor& c) {
        b->setText(c.alpha() < 255 ? QString("%1 · %2%").arg(c.name()).arg(c.alpha() * 100 / 255) : c.name());
        b->setStyleSheet(QString("QPushButton { background: %1; color: %2; }")
                             .arg(c.name(), c.lightness() > 128 || c.alpha() < 100 ? "black" : "white"));
    };
    show(m_color, m_clip.title.color);
    show(m_outlineColor, m_clip.title.outlineColor);
    show(m_shadowColor, m_clip.title.shadowColor);
    show(m_boxColor, m_clip.title.boxColor);
    show(m_highlight, m_clip.title.highlight);
}

// ---- Folding sections away ----

void ClipInspector::foldSections()
{
    for (QGroupBox* box : m_sections) {
        const bool folded = m_folded.contains(box->objectName());
        box->setTitle(QString(folded ? "▶  " : "▼  ") + box->property("heading").toString());
        if (folded) {
            for (QWidget* inside : box->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                inside->hide();
            box->setFixedHeight(QFontMetrics(box->font()).height() + 22); // (just the title)
        } else {
            box->setMinimumHeight(0);
            box->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }
}

void ClipInspector::toggleSection(QGroupBox* section)
{
    const QString name = section->objectName();
    if (!m_folded.removeOne(name))
        m_folded << name;
    AppPaths::settings().setValue("properties/folded", m_folded);
    if (!m_folded.contains(name)) {
        // Opening up: show it all, then let the clip hide whatever doesn't apply to it
        for (QWidget* inside : section->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            inside->show();
        m_spotControls->setVisible(m_zoomPreset->currentData() == "spot");
        showClip(m_index, m_clip, m_part);
    } else {
        foldSections();
    }
}

bool ClipInspector::eventFilter(QObject* watched, QEvent* event)
{
    auto* box = qobject_cast<QGroupBox*>(watched);
    if (box && m_sections.contains(box) && event->type() == QEvent::MouseButtonPress) {
        // Only the title strip at the top folds it (clicks anywhere else are for the settings)
        const int titleHeight = QFontMetrics(box->font()).height() + 18;
        if (static_cast<QMouseEvent*>(event)->position().y() <= titleHeight) {
            toggleSection(box);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---- Looks ----

void ClipInspector::fillLooks(const QString& selectLut, int selectLook)
{
    QSignalBlocker hush(m_look);
    m_look->clear();
    const char* builtIn[] = { "None", "Black & white", "Sepia", "Vintage", "Vivid", "Cool", "Warm", "Faded", "Dramatic" };
    for (int i = 0; i < VE_LOOK_COUNT; ++i)
        m_look->addItem(builtIn[i], i);
    // Yours: every .cube in the looks folder (read fresh, so new ones show up straight away)
    QStringList files;
    for (const QFileInfo& f : QDir(AppPaths::looks()).entryInfoList({ "*.cube", "*.CUBE" }, QDir::Files, QDir::Name))
        files << f.absoluteFilePath();
    if (!selectLut.isEmpty() && !files.contains(selectLut))
        files << selectLut; // (used in this project, but kept somewhere else)
    if (!files.isEmpty())
        m_look->insertSeparator(m_look->count());
    for (const QString& path : files)
        m_look->addItem(QFileInfo(path).completeBaseName() + (QFileInfo::exists(path) ? "" : " (missing)"), "lut:" + path);
    m_look->insertSeparator(m_look->count());
    m_look->addItem("Open the looks folder…", "folder");
    const int at = selectLut.isEmpty() ? m_look->findData(std::clamp(selectLook, 0, VE_LOOK_COUNT - 1))
                                       : m_look->findData("lut:" + selectLut);
    m_look->setCurrentIndex(std::max(0, at));
}

// ---- Green screen: finding the colour ----

QColor ClipInspector::guessScreenColour(const QImage& frame)
{
    const QImage img = frame.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    if (w < 8 || h < 8)
        return {};
    // Just the ring round the edge (that's where the screen shows, rather than the person), sorted
    // into rough colour boxes (8 levels per channel), to find the one most of the edge is
    QHash<int, int> boxes;
    QList<QRgb> ring;
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const bool edgeRow = y < h * 0.15 || y > h * 0.85;
        for (int x = 0; x < w; ++x) {
            if (!edgeRow && x > w * 0.15 && x < w * 0.85)
                continue;
            ring << row[x];
            boxes[(qRed(row[x]) >> 5) << 6 | (qGreen(row[x]) >> 5) << 3 | (qBlue(row[x]) >> 5)] += 1;
        }
    }
    // The busiest box and its neighbours (a lit screen's a little uneven, so it spills over a few)
    auto box = [](QRgb c) { return std::array<int, 3> { qRed(c) >> 5, qGreen(c) >> 5, qBlue(c) >> 5 }; };
    int best = -1, bestCount = 0;
    for (auto it = boxes.cbegin(); it != boxes.cend(); ++it)
        if (it.value() > bestCount) {
            best = it.key();
            bestCount = it.value();
        }
    if (best < 0)
        return {};
    const std::array<int, 3> centre { best >> 6, (best >> 3) & 7, best & 7 };
    long sum[3] = {}, count = 0;
    for (QRgb c : ring) {
        const auto b = box(c);
        if (std::abs(b[0] - centre[0]) <= 1 && std::abs(b[1] - centre[1]) <= 1 && std::abs(b[2] - centre[2]) <= 1) {
            sum[0] += qRed(c);
            sum[1] += qGreen(c);
            sum[2] += qBlue(c);
            ++count;
        }
    }
    // A screen fills most of the edge; an ordinary shot's edge is all sorts
    if (count < ring.size() * 0.35)
        return {};
    return QColor(int(sum[0] / count), int(sum[1] / count), int(sum[2] / count));
}

void ClipInspector::findScreenColour()
{
    if (m_index < 0)
        return;
    ve_reader* reader = ve_reader_open(m_clip.path.toUtf8().constData());
    QImage frame;
    if (reader) {
        // The frame under the playhead (or the clip's first, if the playhead's elsewhere)
        const double into = std::clamp(local(), 0.0, std::max(0.0, m_clip.duration));
        std::vector<uint8_t> px(size_t(480) * 270 * 4);
        int fw = 0, fh = 0;
        if (ve_reader_frame(reader, m_clip.in + into * m_clip.speed, 1, 480, 270, px.data(), &fw, &fh) == VE_OK && fw > 0)
            frame = QImage(px.data(), fw, fh, QImage::Format_RGBA8888).copy();
        ve_reader_close(reader);
    }
    const QColor screen = guessScreenColour(frame);
    if (!screen.isValid()) {
        QMessageBox::information(this, "Green screen",
                                 "Couldn't spot a green or blue screen round the edges of this frame. Try "
                                 "\"Pick\" and click the background in the preview instead.");
        return;
    }
    setKeyColor(screen);
}

// ---- Frame ----

void ClipInspector::refreshBorderColorButton()
{
    m_borderColor->setText(m_clip.borderColor.name());
    m_borderColor->setStyleSheet(QString("QPushButton { background: %1; color: %2; }")
                                     .arg(m_clip.borderColor.name(), m_clip.borderColor.lightness() > 128 ? "black" : "white"));
}

void ClipInspector::applyFacecam(int preset)
{
    if (m_index < 0)
        return;
    // 0-3 circles, 4-7 rounded boxes; corners go bottom right, bottom left, top right, top left
    const bool circle = preset < 4;
    const bool right = preset % 4 == 0 || preset % 4 == 2, bottom = preset % 4 < 2;
    const double size = 0.3, margin = 0.03;
    // (as if the project's the same shape as the clip, which a facecam over gameplay usually is)
    const double aspect = m_clip.width > 0 && m_clip.height > 0 ? double(m_clip.width) / m_clip.height : 16.0 / 9.0;
    m_loading = true;
    m_shape->setCurrentIndex(m_shape->findData(circle ? VE_SHAPE_CIRCLE : VE_SHAPE_ROUNDED));
    m_corners->setValue(18);
    m_border->setValue(2.5);
    m_frameShadow->setValue(60);
    m_clip.borderColor = Qt::white;
    refreshBorderColorButton();
    // Circles get cropped square (round the middle); boxes keep the whole picture
    const double side = circle && aspect > 1 ? (1 - 1 / aspect) / 2 * 100 : 0;
    m_cropLeft->setValue(side);
    m_cropRight->setValue(side);
    m_cropTop->setValue(0);
    m_cropBottom->setValue(0);
    // Tucked into the corner, the same gap from both edges
    const double wide = circle ? size / aspect : size; // (how much of the frame's width it takes up)
    m_scale->setValue(size * 100);
    m_posX->setValue((right ? 1 : -1) * (0.5 - margin - wide / 2) * 100);
    m_posY->setValue((bottom ? 1 : -1) * (0.5 - margin * aspect - size / 2) * 100);
    m_loading = false;
    if (auto* form = qobject_cast<QFormLayout*>(m_frameBox->layout()))
        form->setRowVisible(m_corners, !circle);
    apply("facecam"); // (one undo step for all of it)
}
