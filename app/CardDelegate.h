// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QStyledItemDelegate>

// Draws media and library items as little cards: a rounded picture, a length badge and a
// kind badge on it (when the item has them), and the name underneath.
class CardDelegate : public QStyledItemDelegate {
public:
    // Optional extras an item can carry
    enum Role {
        DurationRole = Qt::UserRole + 50, // seconds (double), shown as a badge
        KindRole,                         // "video", "audio" or "picture", shown as a little badge
    };

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
