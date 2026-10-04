// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "HelpWindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QPointer>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const QString OnGitHub = "https://github.com/Shayan-Mazahir/MixMedia-Video-Editor/blob/main/docs/";
}

HelpWindow::HelpWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("MixMedia help");
    resize(760, 680);

    m_browser = new QTextBrowser;
    m_browser->setSearchPaths({ ":/docs" });
    m_browser->setOpenLinks(true);
    m_browser->setOpenExternalLinks(true); // (web links open in your browser)
    QFont font = m_browser->font();
    font.setPointSizeF(font.pointSizeF() * 1.1);
    m_browser->setFont(font);
    m_browser->document()->setDocumentMargin(18);

    auto* back = new QPushButton("◀ Back");
    auto* forward = new QPushButton("Forward ▶");
    auto* home = new QPushButton("Contents");
    auto* web = new QPushButton("Open on GitHub");
    web->setToolTip("The same page on the website");
    auto* bar = new QHBoxLayout;
    bar->addWidget(back);
    bar->addWidget(forward);
    bar->addWidget(home);
    bar->addStretch();
    bar->addWidget(web);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(m_browser, 1);

    connect(back, &QPushButton::clicked, m_browser, &QTextBrowser::backward);
    connect(forward, &QPushButton::clicked, m_browser, &QTextBrowser::forward);
    connect(m_browser, &QTextBrowser::backwardAvailable, back, &QPushButton::setEnabled);
    connect(m_browser, &QTextBrowser::forwardAvailable, forward, &QPushButton::setEnabled);
    connect(home, &QPushButton::clicked, this, [this] { show("README.md"); });
    connect(web, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(OnGitHub + currentPage())); });
    back->setEnabled(false);
    forward->setEnabled(false);
}

void HelpWindow::show(const QString& page)
{
    m_browser->setSource(QUrl(page), QTextDocument::MarkdownResource);
    QWidget::show();
    raise();
    activateWindow();
}

QString HelpWindow::currentPage() const
{
    return m_browser->source().fileName();
}

void HelpWindow::open(const QString& page)
{
    static QPointer<HelpWindow> window;
    if (!window) {
        window = new HelpWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
    }
    window->show(page);
}

QToolButton* HelpWindow::button(const QString& page, QWidget* parent)
{
    return button([page] { return page; }, parent);
}

QToolButton* HelpWindow::button(std::function<QString()> page, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setText("?");
    b->setToolTip("Help with this");
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(22, 22);
    b->setObjectName("help"); // (round, styled by the theme)
    QObject::connect(b, &QToolButton::clicked, b, [page] { open(page()); });
    return b;
}
