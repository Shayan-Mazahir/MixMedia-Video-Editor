// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;

// Your own keyboard shortcuts: every menu action can have different keys, saved in
// settings.ini ([shortcuts]). Anything you haven't changed keeps its usual keys.
namespace Shortcuts {

struct Entry {
    QAction* action;
    QString id;   // what it's saved as, e.g. "split" (stays the same even if the menu text changes)
    QString name; // what people see, e.g. "Split"
    QList<QKeySequence> defaults;
};

// Call for each action as it's made: remembers its usual keys, then applies yours (if you've
// changed them). `tip` is its tooltip without the keys (they're added on the end).
void add(QAction* action, const QString& tip);
QList<Entry> all();

// Changes an action's keys (an empty list = no shortcut), saving them. Same as the usual
// keys = forgets the change.
void set(const QString& id, const QList<QKeySequence>& keys);
void resetAll();

// Which other action already uses these keys ("" = none)
QString whoUses(const QKeySequence& keys, const QString& exceptId);

} // namespace Shortcuts
