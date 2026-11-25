// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Duplicates.h"

#include <QDialog>
#include <QFutureWatcher>
#include <atomic>
#include <memory>

class Library;
class Player;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Tools › Find Duplicates: groups of copies of the same song, the worse copies
// pre-checked for removal. Nothing is touched until "Move to Trash".
class DuplicatesDialog : public QDialog {
    Q_OBJECT
public:
    DuplicatesDialog(Library *library, Player *player, QWidget *parent = nullptr);
    ~DuplicatesDialog() override;

private:
    struct Scanned {
        QVector<Duplicates::Group> groups;
        TrackList tracks;  // snapshot the scan ran on (indices refer to this)
    };

    void startScan();
    void populate();
    void applyPreference();
    void updateSummary();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void moveToTrash();
    QString memberPath(const QTreeWidgetItem *item) const;

    Library *m_library;
    Player *m_player;
    QLabel *m_status;
    QProgressBar *m_progress;
    QComboBox *m_prefer;
    QCheckBox *m_includeSongs;
    QTreeWidget *m_tree;
    QLabel *m_summary;
    QPushButton *m_trash;
    QPushButton *m_rescan;

    Scanned m_data;
    QFutureWatcher<Scanned> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_populating = false;
};
