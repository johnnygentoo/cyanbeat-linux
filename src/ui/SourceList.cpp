// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SourceList.h"

#include "library/TrackModel.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QMenu>
#include <QMimeData>

namespace {
constexpr int KindRole = Qt::UserRole + 1;
constexpr int SmartRole = Qt::UserRole + 2;
constexpr int IdRole = Qt::UserRole + 3;
constexpr int HeaderKind = 99;
constexpr int PhoneKind = 98;
constexpr int PhoneIdRole = Qt::UserRole + 4;

QIcon icon(const char *name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name)));
}

QTreeWidgetItem *sectionHeader(QTreeWidget *tree, const QString &text)
{
    auto *item = new QTreeWidgetItem(tree, {text});
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
    item->setData(0, KindRole, HeaderKind);
    QFont f = tree->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    item->setFont(0, f);
    item->setForeground(0, QColor(21, 244, 238, 150));
    return item;
}
}

QString SourceList::smartName(int smart)
{
    switch (smart) {
    case Source::RecentlyAdded: return tr("Recently Added");
    case Source::RecentlyPlayed: return tr("Recently Played");
    default: return tr("Top 25 Most Played");
    }
}

SourceList::SourceList(QWidget *parent) : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("SourceList"));
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setIndentation(10);
    setIconSize(QSize(16, 16));
    setMinimumWidth(170);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    setTextElideMode(Qt::ElideRight);

    auto *lib = sectionHeader(this, tr("LIBRARY"));
    m_music = new QTreeWidgetItem(lib, {tr("Music")});
    m_music->setIcon(0, icon("music"));
    m_music->setData(0, KindRole, Source::Library);

    m_devicesHeader = sectionHeader(this, tr("DEVICES"));
    setPhones({});

    m_playlistsHeader = sectionHeader(this, tr("PLAYLISTS"));
    for (int s = 0; s < Source::SmartCount; ++s) {
        auto *item = new QTreeWidgetItem(m_playlistsHeader, {smartName(s)});
        item->setIcon(0, icon("smart"));
        item->setData(0, KindRole, Source::Smart);
        item->setData(0, SmartRole, s);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }

    expandAll();
    setCurrentItem(m_music);

    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur) {
        if (m_rebuilding || !cur || cur->data(0, KindRole).toInt() == HeaderKind)
            return;
        Q_EMIT sourceChanged(sourceOf(cur));
    });
    // Phones aren't a view of the library: clicking one opens Sync.
    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (item && item->data(0, KindRole).toInt() == PhoneKind)
            Q_EMIT phoneActivated(item->data(0, PhoneIdRole).toString());
    });
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) {
        if (m_rebuilding || item->data(0, KindRole).toInt() != Source::Playlist)
            return;
        const QString name = item->text(0).trimmed();
        if (!name.isEmpty())
            Q_EMIT renameRequested(item->data(0, IdRole).toLongLong(), name);
    });
}

Source SourceList::sourceOf(const QTreeWidgetItem *item)
{
    Source s;
    if (!item)
        return s;
    s.kind = Source::Kind(item->data(0, KindRole).toInt());
    s.smart = item->data(0, SmartRole).toInt();
    s.id = item->data(0, IdRole).toLongLong();
    return s;
}

Source SourceList::current() const
{
    return sourceOf(currentItem());
}

QTreeWidgetItem *SourceList::itemFor(const Source &s) const
{
    if (s.kind == Source::Library)
        return m_music;
    for (int i = 0; i < m_playlistsHeader->childCount(); ++i) {
        QTreeWidgetItem *it = m_playlistsHeader->child(i);
        if (sourceOf(it) == s)
            return it;
    }
    return nullptr;
}

void SourceList::select(const Source &s)
{
    if (QTreeWidgetItem *it = itemFor(s))
        setCurrentItem(it);
}

void SourceList::setPlaylists(const QVector<Playlist> &playlists)
{
    const Source keep = current();
    m_rebuilding = true;
    for (QTreeWidgetItem *it : std::as_const(m_playlistItems))
        delete it;
    m_playlistItems.clear();
    for (const Playlist &p : playlists) {
        auto *item = new QTreeWidgetItem(m_playlistsHeader, {p.name});
        item->setIcon(0, icon("playlist"));
        item->setData(0, KindRole, Source::Playlist);
        item->setData(0, IdRole, p.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDropEnabled);
        m_playlistItems << item;
    }
    QTreeWidgetItem *again = itemFor(keep);
    setCurrentItem(again ? again : m_music);
    m_rebuilding = false;
    if (!again)
        Q_EMIT sourceChanged(current());
}

void SourceList::setPhones(const QVector<PhoneEntry> &phones)
{
    m_rebuilding = true;
    qDeleteAll(m_devicesHeader->takeChildren());
    if (phones.isEmpty()) {
        auto *none = new QTreeWidgetItem(m_devicesHeader, {tr("No phone connected")});
        none->setIcon(0, icon("phone"));
        none->setFlags(Qt::NoItemFlags);
        none->setData(0, KindRole, HeaderKind);
    }
    for (const PhoneEntry &p : phones) {
        auto *item = new QTreeWidgetItem(m_devicesHeader, {p.label});
        item->setIcon(0, icon("phone"));
        item->setData(0, KindRole, PhoneKind);
        item->setData(0, PhoneIdRole, p.id);
        item->setFlags(Qt::ItemIsEnabled);
        item->setToolTip(0, p.connected ? tr("Click to sync") : tr("Not connected — click to reconnect or sync"));
        if (!p.connected)
            item->setForeground(0, QColor(21, 244, 238, 120));
    }
    m_devicesHeader->setExpanded(true);
    m_rebuilding = false;
}

void SourceList::beginRename(qint64 playlistId)
{
    Source s;
    s.kind = Source::Playlist;
    s.id = playlistId;
    if (QTreeWidgetItem *it = itemFor(s)) {
        setCurrentItem(it);
        editItem(it);
    }
}

// ---------------------------------------------------------------- drops

void SourceList::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(TrackModel::MimeType))
        e->acceptProposedAction();
    else
        e->ignore();
}

void SourceList::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(TrackModel::MimeType)) {
        e->ignore();
        return;
    }
    QTreeWidgetItem *it = itemAt(e->position().toPoint());
    const int kind = it ? it->data(0, KindRole).toInt() : -1;
    // A playlist (append), the PLAYLISTS header or empty space (new playlist).
    const bool ok = !it || kind == Source::Playlist || it == m_playlistsHeader;
    if (ok) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void SourceList::dropEvent(QDropEvent *e)
{
    const TrackModel::DragPayload p = TrackModel::decode(e->mimeData());
    if (p.tracks.isEmpty()) {
        e->ignore();
        return;
    }
    QTreeWidgetItem *it = itemAt(e->position().toPoint());
    if (it && it->data(0, KindRole).toInt() == Source::Playlist)
        Q_EMIT dropOnPlaylist(it->data(0, IdRole).toLongLong(), p.tracks);
    else
        Q_EMIT dropOnNewPlaylist(p.tracks);
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

// ---------------------------------------------------------------- menus / keys

void SourceList::contextMenuEvent(QContextMenuEvent *e)
{
    QTreeWidgetItem *it = itemAt(e->pos());
    QMenu menu(this);
    if (it && it->data(0, KindRole).toInt() == Source::Playlist) {
        const qint64 id = it->data(0, IdRole).toLongLong();
        menu.addAction(tr("Rename"), this, [this, it] { editItem(it); });
        menu.addAction(tr("Delete Playlist…"), this, [this, id] { Q_EMIT deleteRequested(id); });
        menu.addSeparator();
    }
    menu.addAction(tr("New Playlist"), this, &SourceList::newPlaylistRequested);
    menu.exec(e->globalPos());
}

void SourceList::keyPressEvent(QKeyEvent *e)
{
    QTreeWidgetItem *it = currentItem();
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && it
        && it->data(0, KindRole).toInt() == Source::Playlist && state() != QAbstractItemView::EditingState) {
        Q_EMIT deleteRequested(it->data(0, IdRole).toLongLong());
        return;
    }
    QTreeWidget::keyPressEvent(e);
}
