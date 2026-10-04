// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLayout>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>

namespace Theme {

namespace {

const Colours DarkColours {
    true,
    QColor(0x14, 0x15, 0x19), QColor(0x1b, 0x1d, 0x22), QColor(0x24, 0x27, 0x2e), QColor(0x2d, 0x31, 0x39),
    QColor(0x31, 0x35, 0x3d), QColor(0xe6, 0xe7, 0xea), QColor(0x8b, 0x90, 0x99), QColor(0x2f, 0xc6, 0xb4),
    QColor(0x2b, 0x9f, 0xd8),
    // timeline: tracks, FX tracks, track names, ruler, lines, name text, ruler text, ticks
    QColor(0x18, 0x1a, 0x1f), QColor(0x1a, 0x19, 0x1c), QColor(0x1e, 0x21, 0x27), QColor(0x1d, 0x20, 0x26),
    QColor(0x2a, 0x2d, 0x34), QColor(0xc4, 0xc8, 0xcf), QColor(0xa4, 0xa9, 0xb2), QColor(0x5a, 0x60, 0x6b),
    QColor(0xd8, 0xdb, 0xe0),
};

const Colours LightColours {
    false,
    QColor(0xe9, 0xec, 0xf0), QColor(0xf8, 0xf9, 0xfb), QColor(0xff, 0xff, 0xff), QColor(0xe6, 0xea, 0xef),
    QColor(0xd3, 0xd8, 0xdf), QColor(0x1d, 0x21, 0x29), QColor(0x68, 0x6f, 0x7b), QColor(0x12, 0xa3, 0x92),
    QColor(0x1f, 0x86, 0xc7),
    QColor(0xf1, 0xf3, 0xf6), QColor(0xf4, 0xf1, 0xec), QColor(0xe4, 0xe8, 0xed), QColor(0xe0, 0xe4, 0xea),
    QColor(0xd2, 0xd7, 0xde), QColor(0x2e, 0x34, 0x3d), QColor(0x4d, 0x54, 0x5f), QColor(0x9a, 0xa1, 0xab),
    QColor(0x3a, 0x40, 0x4a),
};

const Colours* g_current = &DarkColours;

// The few little pictures a style sheet can't draw on its own (ticks, dots, arrows), made here
// so there are no image files to ship. Drawn big and shown small, so they're sharp on any screen.
QString makeImages(const Colours& c)
{
    QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(c.dark ? "theme-dark" : "theme-light");
    QDir().mkpath(dir);
    auto draw = [&](const QString& name, auto&& paint) {
        QImage img(48, 48, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        paint(p);
        p.end();
        img.save(QDir(dir).filePath(name));
    };
    draw("check.png", [](QPainter& p) {
        p.setPen(QPen(QColor(0x10, 0x12, 0x14), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF tick[] = { { 11, 25 }, { 20, 34 }, { 37, 14 } };
        p.drawPolyline(tick, 3);
    });
    draw("dot.png", [](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x10, 0x12, 0x14));
        p.drawEllipse(QPointF(24, 24), 9, 9);
    });
    auto arrow = [](bool down, QColor colour) {
        return [down, colour](QPainter& p) {
            p.setPen(QPen(colour, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF a[] = { { 14, down ? 19.0 : 29.0 }, { 24, down ? 29.0 : 19.0 }, { 34, down ? 19.0 : 29.0 } };
            p.drawPolyline(a, 3);
        };
    };
    draw("down.png", arrow(true, c.text));
    draw("up.png", arrow(false, c.text));
    draw("down-dim.png", arrow(true, c.dim));
    return dir;
}

QString styleSheet(const Colours& c, const QString& images)
{
    QString s = R"(
* { outline: none; }
QMainWindow, QDialog { background: @window; }
QWidget { color: @text; }

/* ---- Menus and the toolbar ---- */
QMenuBar { background: @window; border-bottom: 1px solid @border; padding: 2px 4px; }
QMenuBar::item { padding: 5px 10px; border-radius: 5px; background: transparent; }
QMenuBar::item:selected { background: @hover; }
QMenu { background: @panel; border: 1px solid @border; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 26px 6px 14px; border-radius: 5px; }
QMenu::item:selected { background: @hover; }
QMenu::item:disabled { color: @dim; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }

QToolBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @toolbartop, stop:1 @window);
           border: none; border-bottom: 1px solid @border; padding: 5px 8px; spacing: 3px; }
QToolBar::separator { width: 1px; background: @border; margin: 6px 6px; }
QToolBar QToolButton { padding: 5px 9px; border-radius: 6px; border: 1px solid transparent; }
QToolBar QToolButton:hover { background: @hover; border-color: @border; }
QToolBar QToolButton:pressed { background: @control; }
QToolButton#primary { color: #071312; font-weight: bold; padding: 6px 20px; border-radius: 7px; border: none;
                      background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QToolButton#primary:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #45d8c6, stop:1 #45b2e6); }
QToolButton#play { border-radius: 15px; border: none;
                   background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QToolButton#play:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #45d8c6, stop:1 #45b2e6); }
QToolButton#help { border: 1px solid @accent; border-radius: 11px; color: @accent; font-weight: bold;
                   background: transparent; padding: 0; }
QToolButton#help:hover { background: @accent; color: #071312; }

/* ---- Tabs ---- */
QTabWidget::pane { border: none; background: @panel; border-radius: 8px; top: -1px; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @dim; padding: 7px 9px; margin: 4px 0 0 0;
               border-top-left-radius: 7px; border-top-right-radius: 7px; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: @text; background: @tint; }
QTabBar::tab:selected { color: @text; background: @panel; border-bottom: 2px solid @accent; }

/* ---- Buttons ---- */
QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @buttontop, stop:1 @buttonbottom);
              border: 1px solid @border; border-radius: 6px; padding: 5px 12px; min-height: 18px; }
QPushButton:hover { background: @hover; border-color: @strong; }
QPushButton:pressed { background: @control; }
QPushButton:disabled { color: @dim; background: @control; border-color: @border; }
QPushButton:checked { color: #071312; border: none;
                      background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QPushButton:default { border-color: @accent; }
QPushButton#bigPrimary { color: #071312; font-weight: bold; border: none;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QPushButton#bigPrimary:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #45d8c6, stop:1 #45b2e6); }
QPushButton[role="link"] { background: transparent; border: none; color: @accent; padding: 3px 6px; }
QPushButton[role="link"]:hover { text-decoration: underline; }
QPushButton[role="link"]:disabled { color: @dim; }
QToolButton { border-radius: 6px; padding: 3px; }
QToolButton:hover { background: @hover; }

/* ---- Text boxes, number boxes and drop-downs ---- */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: @control; border: 1px solid @border; border-radius: 6px; padding: 4px 7px;
    selection-background-color: @accent; selection-color: #071312; }
QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: @accent; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @dim; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: @strong; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url(@img/down.png); width: 11px; height: 11px; }
QComboBox::down-arrow:disabled { image: url(@img/down-dim.png); }
QComboBox QAbstractItemView { background: @panel; border: 1px solid @border; border-radius: 6px; padding: 4px;
                              selection-background-color: @hover; selection-color: @text; outline: none; }
QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button {
    border: none; width: 16px; background: transparent; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(@img/up.png); width: 9px; height: 9px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(@img/down.png); width: 9px; height: 9px; }

/* ---- Tick boxes and choices ---- */
QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 15px; height: 15px; background: @control; border: 1px solid @indicator; }
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { image: url(@img/check.png); border: none;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QRadioButton::indicator:checked { image: url(@img/dot.png); border: none;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QCheckBox:disabled, QRadioButton:disabled { color: @dim; }

/* ---- Sliders ---- */
QSlider::groove:horizontal { height: 4px; background: @groove; border-radius: 2px; }
QSlider::sub-page:horizontal { height: 4px; border-radius: 2px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent2, stop:1 @accent); }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; border-radius: 7px;
    background: qradialgradient(cx:0.5, cy:0.4, radius:0.6, fx:0.5, fy:0.3, stop:0 #ffffff, stop:1 #cfd3d8);
    border: 1px solid @handleborder; }
QSlider::handle:horizontal:hover { background: #ffffff; border: 2px solid @accent; }
QSlider::sub-page:horizontal:disabled { background: @groove; }
QSlider::handle:horizontal:disabled { background: @dim; }

/* ---- Sections in Properties (and other boxes) ---- */
QGroupBox { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @cardtop, stop:1 @cardbottom);
            border: 1px solid @border; border-radius: 9px; margin-top: 8px; padding: 30px 4px 6px 4px; font-weight: bold; }
QGroupBox::title { subcontrol-origin: padding; subcontrol-position: top left; left: 12px; top: 9px; padding: 0;
                   color: @accent; font-size: @ptgroup; letter-spacing: 0.5px; }
QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QRadioButton, QGroupBox QPushButton { font-weight: normal; }

/* ---- Lists, tables and the library ---- */
QListWidget, QListView, QTableWidget, QTableView, QTreeView, QTextBrowser {
    background: @panel; border: none; border-radius: 8px; alternate-background-color: @cardbottom; }
QListWidget::item { border-radius: 8px; padding: 4px; color: @text; }
QListWidget::item:hover { background: @tint; }
QListWidget::item:selected { background: rgba(47, 198, 180, 0.18); color: @text; }
QTableWidget { gridline-color: @grid; selection-background-color: rgba(47, 198, 180, 0.22); selection-color: @text; }
QHeaderView::section { background: @control; color: @dim; border: none; border-right: 1px solid @border;
                       padding: 5px 8px; font-weight: bold; }

/* ---- Scrollbars: thin, out of the way ---- */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: @scroll; border-radius: 3px; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: @scrollhover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

/* ---- Odds and ends ---- */
QWidget#card { background: @panel; border-radius: 10px; }
QSplitter { background: @window; }
QSplitter::handle { background: @window; }
QSplitter::handle:hover { background: @accent; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical { height: 4px; }
QProgressBar { background: @control; border: 1px solid @border; border-radius: 6px; text-align: center; height: 16px; }
QProgressBar::chunk { border-radius: 5px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent2, stop:1 @accent); }
QToolTip { background: @panel; color: @text; border: 1px solid @border; border-radius: 6px; padding: 6px 8px; }
QStatusBar { background: @window; color: @dim; border-top: 1px solid @border; }
QStatusBar::item { border: none; }

QLabel[role="hint"] { color: @dim; font-size: @ptsmall; }
QLabel[role="dim"] { color: @dim; }
QLabel[role="heading"] { font-weight: bold; font-size: @ptheading; letter-spacing: 0.5px; }
QLabel[role="accent"] { font-weight: bold; color: @accent; font-size: @ptheading; }
QLabel[role="big"] { font-weight: bold; font-size: @ptbig; }
/* (no icons on OK/Cancel buttons, some desktops add them and they clash) */
QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }
)";
    // Fill in the colours (@name) for this look
    const bool dark = c.dark;
    const QHash<QString, QString> names = {
        { "window", c.window.name() }, { "panel", c.panel.name() }, { "control", c.control.name() },
        { "hover", c.hover.name() }, { "border", c.border.name() }, { "text", c.text.name() }, { "dim", c.dim.name() },
        { "accent", c.accent.name() }, { "accent2", c.accent2.name() },
        { "toolbartop", dark ? "#1d2026" : "#f6f7f9" },
        { "cardtop", dark ? "#20232a" : "#ffffff" }, { "cardbottom", dark ? "#1c1e24" : "#f9fafb" },
        { "buttontop", dark ? "#2a2e36" : "#ffffff" }, { "buttonbottom", dark ? "#24272e" : "#f1f3f6" },
        { "strong", dark ? "#3c414b" : "#b9c0ca" }, { "indicator", dark ? "#454a54" : "#aab2bd" },
        { "groove", dark ? "#31353e" : "#d5dae1" }, { "handleborder", dark ? "#0d0f12" : "#aab2bd" },
        { "grid", dark ? "#262930" : "#e3e7ec" }, { "scroll", dark ? "#3a3f48" : "#c4cad3" },
        { "scrollhover", dark ? "#4a505b" : "#a9b0ba" },
        { "tint", dark ? "rgba(255, 255, 255, 0.05)" : "rgba(0, 0, 0, 0.04)" },
        { "img", QDir::fromNativeSeparators(images) },
        // Text sizes, relative to the normal font (so they follow the system's size)
        { "ptsmall", QString::number(font(0.88).pointSizeF(), 'f', 1) + "pt" },
        { "ptgroup", QString::number(font(0.98).pointSizeF(), 'f', 1) + "pt" },
        { "ptheading", QString::number(font(1.08).pointSizeF(), 'f', 1) + "pt" },
        { "ptbig", QString::number(font(2.0).pointSizeF(), 'f', 1) + "pt" },
    };
    static const QRegularExpression token("@([a-z0-9]+)");
    QString out;
    qsizetype last = 0;
    for (const QRegularExpressionMatch& m : token.globalMatch(s)) {
        out += s.mid(last, m.capturedStart() - last);
        out += names.value(m.captured(1), m.captured(0));
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

} // namespace

QFont font(double scale, bool bold)
{
    QFont f = QApplication::font();
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() * scale);
    else
        f.setPixelSize(std::max(6, int(f.pixelSize() * scale)));
    f.setBold(bold);
    return f;
}

void fit(QWidget& window)
{
    if (QLayout* layout = window.layout())
        layout->setSizeConstraint(QLayout::SetMinimumSize); // (can't be shrunk past its contents)
    window.adjustSize();
}

const Colours& colours()
{
    return *g_current;
}

Mode savedMode()
{
    QString m = QSettings().value("appearance/theme", "dark").toString();
    return m == "light" ? Mode::Light : m == "system" ? Mode::System : Mode::Dark;
}

void saveMode(Mode mode)
{
    QSettings().setValue("appearance/theme", mode == Mode::Light ? "light" : mode == Mode::System ? "system" : "dark");
}

void apply(QApplication& app, Mode mode)
{
    bool dark = mode == Mode::Dark;
    if (mode == Mode::System)
        dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    g_current = dark ? &DarkColours : &LightColours;
    const Colours& c = *g_current;

    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.control);
    p.setColor(QPalette::AlternateBase, c.panel);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.control);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::ToolTipBase, c.panel);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.dim);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, QColor(0x07, 0x13, 0x12));
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.window);
    for (auto role : { QPalette::Text, QPalette::ButtonText, QPalette::WindowText })
        p.setColor(QPalette::Disabled, role, c.dim);
    app.setPalette(p);
    app.setStyleSheet(styleSheet(c, makeImages(c)));
}

} // namespace Theme
