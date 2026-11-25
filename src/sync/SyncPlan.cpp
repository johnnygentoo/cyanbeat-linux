// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SyncPlan.h"

#include <QHash>
#include <algorithm>

namespace SyncPlan {

QString phoneName(const QString &rel)
{
    QString out = rel;
    for (QChar &c : out) {
        switch (c.unicode()) {
        case '?': c = QChar(0xFF1F); break;
        case ':': c = QChar(0xFF1A); break;
        case '"': c = QChar(0xFF02); break;
        case '*': c = QChar(0xFF0A); break;
        case '<': c = QChar(0xFF1C); break;
        case '>': c = QChar(0xFF1E); break;
        case '|': c = QChar(0xFF5C); break;
        case '\\': c = QChar(0xFF3C); break;
        default: break;
        }
    }
    return out;
}

QVector<Item> plan(const QVector<File> &local, const QVector<File> &remote, const QSet<QString> &synced,
                   const QSet<QString> &removedHere, Mode mode)
{
    QHash<QString, const File *> localByKey, remoteByKey;
    QSet<QString> removedKeys;
    for (const File &f : local)
        localByKey.insert(phoneName(f.rel), &f);
    for (const File &f : remote)
        remoteByKey.insert(f.rel, &f);
    for (const QString &r : removedHere)
        removedKeys.insert(phoneName(r));

    QVector<Item> out;
    for (auto it = localByKey.cbegin(); it != localByKey.cend(); ++it) {
        const File &l = **it;
        const QString &key = it.key();
        if (const File *r = remoteByKey.value(key)) {
            if (r->size != l.size)
                out.push_back({Action::Conflict, l.rel, key, l.size,
                               QStringLiteral("same name, different file (%1 vs %2 bytes) — left alone")
                                   .arg(l.size).arg(r->size),
                               false});
            continue;
        }
        if (mode == Mode::Mirror && synced.contains(key))
            out.push_back({Action::DeleteLocal, l.rel, key, l.size, QStringLiteral("deleted on the phone since the last sync"),
                           true});
        else
            out.push_back({Action::Push, l.rel, key, l.size,
                           synced.contains(key) ? QStringLiteral("deleted on the phone; merge keeps it")
                                                : QStringLiteral("new on this PC"),
                           true});
    }
    for (auto it = remoteByKey.cbegin(); it != remoteByKey.cend(); ++it) {
        if (localByKey.contains(it.key()))
            continue;
        const File &r = **it;
        if (removedKeys.contains(r.rel))
            out.push_back({Action::DeleteRemote, r.rel, r.rel, r.size,
                           QStringLiteral("you removed it on this PC (duplicate)"), true});
        else if (mode == Mode::Mirror && synced.contains(r.rel))
            out.push_back({Action::DeleteRemote, r.rel, r.rel, r.size, QStringLiteral("deleted on this PC since the last sync"),
                           true});
        else
            out.push_back({Action::Pull, r.rel, r.rel, r.size,
                           synced.contains(r.rel) ? QStringLiteral("deleted on this PC; merge keeps it")
                                                  : QStringLiteral("new on the phone"),
                           true});
    }
    std::sort(out.begin(), out.end(), [](const Item &a, const Item &b) {
        if (a.action != b.action)
            return int(a.action) < int(b.action);
        return QString::localeAwareCompare(a.localRel, b.localRel) < 0;
    });
    return out;
}

QSet<QString> afterSync(const QVector<File> &local, const QVector<File> &remote, const QVector<Item> &done)
{
    QSet<QString> localKeys, remoteKeys, conflicts;
    for (const File &f : local)
        localKeys.insert(phoneName(f.rel));
    for (const File &f : remote)
        remoteKeys.insert(f.rel);
    for (const Item &i : done) {
        switch (i.action) {
        case Action::Push: remoteKeys.insert(i.remoteRel); break;
        case Action::Pull: localKeys.insert(i.remoteRel); break;
        case Action::DeleteRemote: remoteKeys.remove(i.remoteRel); break;
        case Action::DeleteLocal: localKeys.remove(i.remoteRel); break;
        case Action::Conflict: conflicts.insert(i.remoteRel); break;
        }
    }
    // Two different files that share a name were never "in sync": keep them out,
    // or a later mirror sync could delete one because the other went away.
    return (localKeys & remoteKeys) - conflicts;
}

} // namespace SyncPlan
