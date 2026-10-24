// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "Shortcuts.h"
#include "AppPaths.h"

#include <QAction>
#include <QPointer>
#include <QRegularExpression>

namespace Shortcuts {

namespace {

struct Kept {
    QPointer<QAction> action;
    QString id, name, tip;
    QList<QKeySequence> defaults;
};

QList<Kept>& kept()
{
    static QList<Kept> list;
    return list;
}

QString keysText(const QList<QKeySequence>& keys)
{
    QStringList parts;
    for (const QKeySequence& k : keys)
        parts << k.toString(QKeySequence::PortableText);
    return parts.join(", ");
}

QList<QKeySequence> fromText(const QString& text)
{
    QList<QKeySequence> keys;
    if (text == "none") // (you took its shortcut away)
        return keys;
    for (const QString& part : text.split(", ", Qt::SkipEmptyParts))
        if (QKeySequence k(part.trimmed(), QKeySequence::PortableText); !k.isEmpty())
            keys << k;
    return keys;
}

void show(const Kept& k, const QList<QKeySequence>& keys)
{
    k.action->setShortcuts(keys);
    k.action->setToolTip(keys.isEmpty() ? k.tip : QString("%1 (%2)").arg(k.tip, keys.first().toString(QKeySequence::NativeText)));
}

} // namespace

void add(QAction* action, const QString& tip)
{
    // "S&plit" -> "Split", "Save project &as…" -> "Save project as"; the id is that, tidied
    QString name = action->text();
    name.remove('&').remove(QChar(0x2026)).remove("...");
    name = name.trimmed();
    QString id = name.toLower();
    id.replace(QRegularExpression("[^a-z0-9]+"), "-");
    id.remove(QRegularExpression("^-|-$"));
    Kept k { action, id, name, tip, action->shortcuts() };
    kept() << k;
    const QSettings s = AppPaths::settings();
    const QString key = "shortcuts/" + id;
    if (s.contains(key))
        show(k, fromText(s.value(key).toString()));
}

QList<Entry> all()
{
    QList<Entry> out;
    for (const Kept& k : kept())
        if (k.action)
            out << Entry { k.action, k.id, k.name, k.defaults };
    return out;
}

void set(const QString& id, const QList<QKeySequence>& keys)
{
    QSettings s = AppPaths::settings();
    for (const Kept& k : kept()) {
        if (k.id != id || !k.action)
            continue;
        show(k, keys);
        if (keysText(keys) == keysText(k.defaults))
            s.remove("shortcuts/" + id);
        else
            s.setValue("shortcuts/" + id, keysText(keys).isEmpty() ? QStringLiteral("none") : keysText(keys));
    }
}

void resetAll()
{
    AppPaths::settings().remove("shortcuts");
    for (const Kept& k : kept())
        if (k.action)
            show(k, k.defaults);
}

QString whoUses(const QKeySequence& keys, const QString& exceptId)
{
    if (keys.isEmpty())
        return {};
    for (const Kept& k : kept())
        if (k.action && k.id != exceptId && k.action->shortcuts().contains(keys))
            return k.name;
    return {};
}

} // namespace Shortcuts
