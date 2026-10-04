// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "SubtitlePanel.h"
#include "HelpWindow.h"
#include "SubtitleFile.h"
#include "TimelineWidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

namespace {
enum Column { StartCol, EndCol, TextCol };
}

SubtitlePanel::SubtitlePanel(TimelineWidget* timeline, QWidget* parent)
    : QWidget(parent)
    , m_timeline(timeline)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 6, 4, 4);

    auto* top = new QHBoxLayout;
    auto* add = new QPushButton("+ Line");
    add->setToolTip("Add a line at the playhead");
    auto* captions = new QPushButton("Auto-captions…");
    captions->setToolTip("Listens to the video and writes the subtitles for you");
    auto* import = new QPushButton("Import…");
    import->setToolTip("Bring in an .srt or .vtt file");
    m_export = new QPushButton("Export…");
    m_export->setToolTip("Save the subtitles as an .srt file");
    for (QPushButton* b : { add, captions, import, m_export })
        top->addWidget(b);
    layout->addLayout(top);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({ "Start", "End", "Text" });
    m_table->horizontalHeader()->setSectionResizeMode(TextCol, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(StartCol, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(EndCol, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    layout->addWidget(m_table, 1);

    auto* bottom = new QHBoxLayout;
    m_delete = new QPushButton("Delete");
    m_shift = new QPushButton("Shift all…");
    m_shift->setToolTip("Move every line earlier or later, for when the whole file is a bit out of sync");
    m_look = new QPushButton("Look…");
    m_look->setToolTip("Change how the subtitles look (shown in Properties)");
    bottom->addWidget(m_delete);
    bottom->addWidget(m_shift);
    bottom->addStretch();
    bottom->addWidget(m_look);
    bottom->addWidget(HelpWindow::button("subtitles.md"));
    layout->addLayout(bottom);
    auto* hint = new QLabel("Double-click to change a time or the words. Times are minutes:seconds.");
    hint->setWordWrap(true);
    hint->setProperty("role", "hint");
    layout->addWidget(hint);

    connect(add, &QPushButton::clicked, this, &SubtitlePanel::addLine);
    connect(captions, &QPushButton::clicked, this, &SubtitlePanel::autoCaptionsClicked);
    connect(import, &QPushButton::clicked, this, &SubtitlePanel::importClicked);
    connect(m_export, &QPushButton::clicked, this, &SubtitlePanel::exportClicked);
    connect(m_delete, &QPushButton::clicked, this, &SubtitlePanel::deleteSelected);
    connect(m_shift, &QPushButton::clicked, this, &SubtitlePanel::shiftAll);
    connect(m_look, &QPushButton::clicked, this, [this] {
        if (!m_lines.isEmpty())
            m_timeline->selectClip(m_lines.value(std::max(0, m_table->currentRow()), m_lines.first()));
    });
    connect(m_table, &QTableWidget::cellChanged, this, &SubtitlePanel::edited);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row) {
        // Jump there, and show it in Properties
        if (row < 0 || row >= m_lines.size())
            return;
        m_timeline->selectClip(m_lines[row]);
        emit seekRequested(m_timeline->clips().at(m_lines[row]).start + 0.01);
    });
    refresh();
}

void SubtitlePanel::refresh()
{
    m_filling = true;
    m_lines = m_timeline->subtitleLines();
    const int row = m_table->currentRow();
    m_table->setRowCount(int(m_lines.size()));
    for (int r = 0; r < m_lines.size(); ++r) {
        const TimelineClip& c = m_timeline->clips().at(m_lines[r]);
        const QString cells[] = { SubtitleFile::formatTime(c.start, false), SubtitleFile::formatTime(c.end(), false), c.title.text };
        for (int col = 0; col < 3; ++col) {
            QTableWidgetItem* item = m_table->item(r, col);
            if (!item) {
                item = new QTableWidgetItem;
                m_table->setItem(r, col, item);
            }
            if (item->text() != cells[col])
                item->setText(cells[col]);
        }
    }
    if (row >= 0 && row < m_table->rowCount())
        m_table->setCurrentCell(row, m_table->currentColumn());
    m_table->resizeRowsToContents();
    for (QPushButton* b : { m_export, m_delete, m_shift, m_look })
        b->setEnabled(!m_lines.isEmpty());
    m_current = -1;
    m_filling = false;
}

void SubtitlePanel::setPlayhead(double sec)
{
    int now = -1;
    for (int r = 0; r < m_lines.size(); ++r) {
        const TimelineClip& c = m_timeline->clips().at(m_lines[r]);
        if (sec >= c.start && sec < c.end())
            now = r;
    }
    if (now == m_current)
        return;
    m_filling = true;
    for (int r : { m_current, now }) {
        if (r < 0 || r >= m_table->rowCount())
            continue;
        for (int col = 0; col < 3; ++col)
            if (QTableWidgetItem* item = m_table->item(r, col))
                item->setBackground(r == now ? QColor(0x2f, 0xc6, 0xb4, 60) : QBrush());
    }
    m_current = now;
    m_filling = false;
    if (now >= 0)
        m_table->scrollToItem(m_table->item(now, TextCol)); // keep up with playback
}

void SubtitlePanel::edited(int row, int column)
{
    if (m_filling || row < 0 || row >= m_lines.size())
        return;
    const int index = m_lines[row];
    TimelineClip c = m_timeline->clips().at(index);
    const QString text = m_table->item(row, column)->text();
    if (column == TextCol) {
        c.title.text = text;
        c.name = text.section('\n', 0, 0);
        c.words.clear(); // (the old word timings don't fit new words)
    } else {
        double t = SubtitleFile::parseTime(text);
        if (t < 0) {
            refresh(); // couldn't read that, put it back
            return;
        }
        if (column == StartCol) {
            double end = c.end();
            c.start = std::min(t, end - 0.05);
            c.duration = end - c.start;
        } else {
            c.duration = std::max(0.05, t - c.start);
        }
    }
    m_timeline->updateClip(index, c, "subtitle:" + QString::number(column));
}

void SubtitlePanel::addLine()
{
    TimelineClip line;
    line.kind = TimelineClip::Kind::Subtitle;
    line.title = TitleStyle::subtitles();
    line.title.text = "New subtitle";
    line.name = line.title.text;
    line.duration = 2.0;
    m_timeline->addAtPlayhead(line);
    // Straight into typing the words
    refresh();
    int index = m_timeline->selectedIndex();
    int row = int(m_lines.indexOf(index));
    if (row >= 0) {
        m_table->setCurrentCell(row, TextCol);
        m_table->editItem(m_table->item(row, TextCol));
    }
}

void SubtitlePanel::deleteSelected()
{
    std::set<int> rows;
    for (const QModelIndex& i : m_table->selectionModel()->selectedIndexes())
        rows.insert(i.row());
    if (rows.empty())
        return;
    m_timeline->selectClip(-1);
    for (int r : rows)
        if (r < m_lines.size())
            m_timeline->toggleSelected(m_lines[r]);
    m_timeline->deleteSelectedKeepGap(); // (subtitles don't close up, they stay at their times)
}

void SubtitlePanel::shiftAll()
{
    bool ok = false;
    double by = QInputDialog::getDouble(this, "Shift all subtitles",
                                        "Seconds to move every line (negative = earlier):", 0.0, -3600, 3600, 3, &ok);
    if (!ok || by == 0.0)
        return;
    QList<TimelineClip> lines;
    for (int i : m_lines) {
        TimelineClip c = m_timeline->clips().at(i);
        c.start = std::max(0.0, c.start + by);
        lines << c;
    }
    m_timeline->addSubtitles(lines, true); // (replaces them all in one undo step)
}
