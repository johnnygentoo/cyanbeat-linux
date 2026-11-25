// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Collection.h"

#include <QTreeWidget>

// What the track table is showing.
struct Source {
    enum Kind { Library, Smart, Playlist };
    enum SmartKind { RecentlyAdded, RecentlyPlayed, TopPlayed, SmartCount };
    Kind kind = Library;
    int smart = 0;
    qint64 id = 0;  // playlist id
    bool operator==(const Source &o) const { return kind == o.kind && smart == o.smart && id == o.id; }
};

// The left sidebar: LIBRARY / DEVICES / PLAYLISTS. Accepts track drags onto a
// playlist (append) or onto empty space / the PLAYLISTS header (new playlist).
class SourceList : public QTreeWidget {
    Q_OBJECT
public:
    explicit SourceList(QWidget *parent = nullptr);

    void setPlaylists(const QVector<Playlist> &playlists);
    struct PhoneEntry {
        QString id, label;
        bool connected = false;
    };
    void setPhones(const QVector<PhoneEntry> &phones);
    Source current() const;
    void select(const Source &s);
    void beginRename(qint64 playlistId);

    static QString smartName(int smart);

Q_SIGNALS:
    void sourceChanged(const Source &s);
    void dropOnPlaylist(qint64 id, const QVector<int> &tracks);
    void dropOnNewPlaylist(const QVector<int> &tracks);
    void newPlaylistRequested();
    void renameRequested(qint64 id, const QString &name);
    void deleteRequested(qint64 id);
    void phoneActivated(const QString &id);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QTreeWidgetItem *itemFor(const Source &s) const;
    static Source sourceOf(const QTreeWidgetItem *item);
    QTreeWidgetItem *m_music = nullptr;
    QTreeWidgetItem *m_playlistsHeader = nullptr;
    QTreeWidgetItem *m_devicesHeader = nullptr;
    QList<QTreeWidgetItem *> m_playlistItems;
    bool m_rebuilding = false;
};
