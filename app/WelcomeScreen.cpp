// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "WelcomeScreen.h"
#include "Icons.h"
#include "Theme.h"

#include <QCheckBox>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QString when(const QDateTime& t)
{
    const QDate day = t.date(), today = QDate::currentDate();
    if (day == today)
        return "Today, " + t.toString("h:mm ap");
    if (day == today.addDays(-1))
        return "Yesterday";
    if (day.daysTo(today) < 7)
        return t.toString("dddd");
    return t.toString("d MMM yyyy");
}

} // namespace

bool WelcomeScreen::showAtStartup()
{
    return QSettings().value("welcome/show", true).toBool();
}

WelcomeScreen::WelcomeScreen(const QStringList& recent, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Welcome to MixMedia");
    setMinimumSize(720, 460);

    // ---- Left: the logo and the big buttons ----
    auto* left = new QVBoxLayout;
    auto* logo = new QLabel;
    QPixmap pm(":/mixmedia.png");
    if (!pm.isNull())
        logo->setPixmap(pm.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto* name = new QLabel("MixMedia");
    name->setProperty("role", "big");
    auto* tagline = new QLabel(QString("Video Editor · v%1").arg(MIXMEDIA_VERSION));
    tagline->setProperty("role", "dim");
    left->addWidget(logo);
    left->addWidget(name);
    left->addWidget(tagline);
    left->addSpacing(22);

    auto big = [this](const QString& icon, const QString& text, const QString& sub, Choice c) {
        auto* b = new QPushButton(Icons::get(icon), "  " + text);
        b->setIconSize(QSize(20, 20));
        b->setToolTip(sub);
        b->setMinimumHeight(44);
        b->setStyleSheet("QPushButton { text-align: left; padding-left: 14px; }");
        b->setFont(Theme::font(1.05));
        connect(b, &QPushButton::clicked, this, [this, c] { pick(c); });
        return b;
    };
    QPushButton* fresh = big("new", "New project", "Start with an empty timeline", Choice::New);
    fresh->setObjectName("bigPrimary");
    fresh->setIcon(Icons::get("new", QColor(0x07, 0x13, 0x12))); // (dark, on the bright button)
    left->addWidget(fresh);
    left->addWidget(big("open", "Open project…", "Open a saved .mixmedia project", Choice::Open));
    left->addWidget(big("import", "Import media…", "Bring in videos, music or pictures and get going", Choice::Import));
    left->addStretch();
    auto* again = new QCheckBox("Show this when MixMedia starts");
    again->setChecked(showAtStartup());
    connect(again, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("welcome/show", on); });
    left->addWidget(again);

    // ---- Right: recent projects ----
    auto* right = new QVBoxLayout;
    auto* heading = new QLabel("Recent projects");
    heading->setProperty("role", "heading");
    right->addWidget(heading);
    auto* list = new QListWidget;
    list->setIconSize(QSize(18, 18));
    list->setSpacing(2);
    for (const QString& path : recent) {
        QFileInfo info(path);
        if (!info.exists())
            continue;
        auto* item = new QListWidgetItem(Icons::get("open", Theme::colours().accent),
                                         QString("%1\n%2 · %3").arg(info.completeBaseName(), when(info.lastModified()),
                                                                    QDir::toNativeSeparators(info.absolutePath())));
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setSizeHint(QSize(0, 48));
        list->addItem(item);
    }
    if (list->count() == 0) {
        auto* none = new QLabel("Projects you save or open will show up here, so you can jump straight back in.");
        none->setWordWrap(true);
        none->setProperty("role", "dim");
        none->setAlignment(Qt::AlignTop);
        right->addWidget(none, 1);
        list->hide();
    }
    right->addWidget(list, 1);
    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        pick(Choice::Recent, item->data(Qt::UserRole).toString());
    });
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        pick(Choice::Recent, item->data(Qt::UserRole).toString());
    });

    auto* card = new QWidget;
    card->setObjectName("card");
    card->setAttribute(Qt::WA_StyledBackground);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->addLayout(right);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(26, 24, 20, 20);
    layout->setSpacing(26);
    layout->addLayout(left, 2);
    layout->addWidget(card, 3);
}

void WelcomeScreen::pick(Choice c, const QString& path)
{
    m_choice = c;
    m_path = path;
    accept();
}
