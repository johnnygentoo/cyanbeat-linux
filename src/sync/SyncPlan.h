// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSet>
#include <QString>
#include <QVector>

// Deciding what a sync will do. Pure (no adb, no disk) so every rule is testable.
namespace SyncPlan {

enum class Mode {
    Merge,   // keep olds, combine news: new files go both ways, nothing is deleted
             // except files you removed on purpose here (e.g. duplicates) — and only on confirm
    Mirror,  // also carry deletions across, using what was in sync last time; confirm first
};

enum class Action { Push, Pull, DeleteRemote, DeleteLocal, Conflict };

struct File {
    QString rel;    // path under the sync root, '/'-separated
    qint64 size = 0;
};

struct Item {
    Action action;
    QString localRel;   // as on this PC
    QString remoteRel;  // as on the phone (names the phone can't store are mapped)
    qint64 size = 0;    // bytes that will move (or the file's size for deletes)
    QString why;
    bool checked = true;
};

// Android's shared storage refuses  ? : " * < > | \  in names; yt-dlp's own
// substitutes are the full-width look-alikes, so use those.
QString phoneName(const QString &rel);

// `synced`: phone-side names that were on both sides after the last sync
// (empty on a first sync). `removedHere`: local rels removed on purpose.
QVector<Item> plan(const QVector<File> &local, const QVector<File> &remote, const QSet<QString> &synced,
                   const QSet<QString> &removedHere, Mode mode);

// Phone-side names on both sides once `items` (the checked ones) have run.
QSet<QString> afterSync(const QVector<File> &local, const QVector<File> &remote, const QVector<Item> &done);

} // namespace SyncPlan
