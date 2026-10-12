// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AboutDialog.h"
#include "AppPaths.h"
#include "HelpWindow.h"
#include "Theme.h"
#include "UpdateChecker.h"

#include <ve/engine.h>

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const QString Repo = "https://github.com/Shayan-Mazahir/MixMedia-Video-Editor";

} // namespace

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("About MixMedia");
    setMinimumWidth(560);
    const Theme::Colours& c = Theme::colours();
    const QString accent = c.accent.name(), dim = c.dim.name();

    // ---- The top: logo, name, version ----
    auto* logo = new QLabel;
    const int logoSize = QFontMetrics(Theme::font()).height() * 6; // (grows with the text size)
    logo->setPixmap(QPixmap(":/mixmedia.png").scaled(logoSize, logoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignTop);
    auto* name = new QLabel("MixMedia Video Editor");
    name->setFont(Theme::font(2.0, true));
    auto* version = new QLabel(QString("Version %1").arg(MIXMEDIA_VERSION));
    version->setFont(Theme::font(1.15));
    version->setStyleSheet("color: " + accent + ";");
    auto* tagline = new QLabel("A video editor made for fun, by one person, in their free time.");
    tagline->setWordWrap(true);
    tagline->setStyleSheet("color: " + dim + ";");
    auto* titles = new QVBoxLayout;
    titles->addStretch();
    titles->addWidget(name);
    titles->addWidget(version);
    titles->addWidget(tagline);
    titles->addStretch();
    auto* top = new QHBoxLayout;
    top->setSpacing(20);
    top->addWidget(logo);
    top->addLayout(titles, 1);

    // ---- The rest: app/about.md (edit that to change what it says) ----
    QFile file(":/about.md");
    QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    text.replace("{version}", MIXMEDIA_VERSION).replace("{qt}", qVersion()).replace("{engine}", ve_version()).replace("{repo}", Repo);
    if (!Theme::hasEmoji()) {
        // No emoji font here: smileys become plain ones, anything else just goes (rather than empty boxes)
        text.replace("🙂", ":)").replace("😄", ":D").replace("😉", ";)");
        text.remove(QRegularExpression("[\\x{1F000}-\\x{1FAFF}\\x{2600}-\\x{27BF}]\\x{FE0F}?"));
    }
    auto* body = new QLabel(text);
    body->setWordWrap(true);
    body->setTextFormat(Qt::MarkdownText);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body->setContentsMargins(0, 0, 14, 0); // (room for the scrollbar, so it never covers the words)
    connect(body, &QLabel::linkActivated, this, &AboutDialog::follow);
    // (scrolls if the screen's small or the text's big)
    auto* scroll = new QScrollArea;
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");

    // ---- Buttons ----
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText("Back to editing");
    QPushButton* help = buttons->addButton("Help pages", QDialogButtonBox::ActionRole);
    QPushButton* updates = buttons->addButton("Check for updates", QDialogButtonBox::ActionRole);
    connect(help, &QPushButton::clicked, this, [] { HelpWindow::open(); });
    connect(updates, &QPushButton::clicked, this, [this] { UpdateChecker::checkNow(this); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 18);
    layout->addLayout(top);
    layout->addSpacing(6);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);
    Theme::fit(*this);
    resize(std::max(width(), 600), std::max(height(), 560));
}

void AboutDialog::follow(const QString& link)
{
    if (link.startsWith("help:"))
        HelpWindow::open(link.mid(5));
    else if (link == "folder:")
        AppPaths::openFolder(this, AppPaths::data());
    else
        QDesktopServices::openUrl(QUrl(link));
}
