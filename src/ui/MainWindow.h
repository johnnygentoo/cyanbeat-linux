// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/Player.h"
#include "ui/SourceList.h"

#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QVector>

class BrowserPanes;
class DuplicatesDialog;
class Equalizer;
class EqualizerDialog;
class QStackedWidget;
class Visualizer;
class VisualizerView;
class LcdDisplay;
class Library;
class QAction;
class QLabel;
class QLineEdit;
class QSlider;
class QSortFilterProxyModel;
class QSplitter;
class QTableView;
class QTimer;
class QToolButton;
class TrackModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Library *library, Player *player, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Testing hook: once the first scan lands, save a screenshot and quit.
    void setSnapshotPath(const QString &path) { m_snapshotPath = path; }

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    QWidget *buildTopBar();
    QWidget *buildSourceList();
    QWidget *buildStatusStrip();
    void setupTable();

    void onSourceChanged(const Source &s);
    void loadSourceRows();    // m_sourceRows/m_sourcePositions for the current source
    void refilter();          // source → search → browser → table
    void onBrowserFiltered();
    void updateReorderable();
    void onReorder(QVector<int> rows, int targetRow);
    void showTrackMenu(const QPoint &pos);
    void showHeaderMenu(const QPoint &pos);
    QVector<int> selectedTracks() const;       // library indices, view order
    QVector<int> selectedPositions() const;    // playlist positions of the selection
    void newPlaylist(const QVector<int> &tracks);
    void removeSelectionFromPlaylist();
    void confirmDeletePlaylist(qint64 id);
    void onLibraryChanged();
    void updateSummary();
    void maybeSnapshot();
    void showDuplicates();
    void showEqualizer();
    void setVisualizerShown(bool on);

    // Playback (the queue lives in Player).
    void playFromRow(int proxyRow);
    void onCurrentChanged(int libraryIndex);
    void onPlayerState(GstEngine::State s);
    void onShuffleChanged(bool on);
    void onRepeatChanged(Player::Repeat r);
    void revealPlaying();

    void loadSettings();
    void saveSettings() const;

    Library *m_library;
    Player *m_player;

    LcdDisplay *m_lcd = nullptr;
    QToolButton *m_playBtn = nullptr;
    QSlider *m_volume = nullptr;
    QLineEdit *m_search = nullptr;
    SourceList *m_sources = nullptr;
    QSplitter *m_mainSplit = nullptr;
    QSplitter *m_contentSplit = nullptr;
    BrowserPanes *m_browser = nullptr;
    QTableView *m_table = nullptr;
    TrackModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QLabel *m_summary = nullptr;
    QToolButton *m_shuffleBtn = nullptr;
    QToolButton *m_repeatBtn = nullptr;
    QAction *m_browserAction = nullptr;
    QAction *m_shuffleAction = nullptr;
    QTimer *m_searchDebounce = nullptr;
    QPointer<DuplicatesDialog> m_dupes;
    QPointer<EqualizerDialog> m_eqDialog;
    Equalizer *m_eq = nullptr;
    Visualizer *m_vis = nullptr;
    QStackedWidget *m_stack = nullptr;
    VisualizerView *m_visView = nullptr;
    QAction *m_visAction = nullptr;
    QAction *m_lcdBarsAction = nullptr;

    QString m_snapshotPath;

    Source m_source;
    QVector<int> m_sourceRows;       // library indices for the current source
    QVector<int> m_sourcePositions;  // per row: playlist position / smart-list rank (-1 in Library)
    QVector<int> m_basePositions;    // after search: positions into m_sourceRows
    int m_librarySortColumn = 4;     // TrackModel::ColArtist
    Qt::SortOrder m_librarySortOrder = Qt::AscendingOrder;
};
