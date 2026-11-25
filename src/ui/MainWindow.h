// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/GstEngine.h"
#include "ui/SourceList.h"

#include <QMainWindow>
#include <QStringList>
#include <QVector>

class BrowserPanes;
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
    explicit MainWindow(Library *library, QWidget *parent = nullptr);
    ~MainWindow() override;

    enum class Repeat { Off, All, One };

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

    // Playback / queue.
    void playFromRow(int proxyRow);
    void playQueueIndex(int pos);
    void togglePlay();
    void stop();
    void next(bool userInitiated);
    void previous();
    void onEndOfStream();
    void onEngineError(const QString &msg);
    void onEngineState(GstEngine::State s);
    void revealPlaying();
    void setShuffle(bool on);
    void cycleRepeat();
    int currentLibraryIndex() const;

    void loadSettings();
    void saveSettings() const;

    Library *m_library;
    GstEngine *m_engine;

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
    QTimer *m_tick = nullptr;
    QTimer *m_searchDebounce = nullptr;

    QVector<int> m_queue;     // library indices, in play order
    int m_queuePos = -1;
    QString m_currentPath;
    QStringList m_queuePaths; // queue parked as paths while a rescan swaps indices
    bool m_shuffle = false;
    Repeat m_repeat = Repeat::Off;
    int m_errorStreak = 0;
    bool m_playCounted = false;

    Source m_source;
    QVector<int> m_sourceRows;       // library indices for the current source
    QVector<int> m_sourcePositions;  // per row: playlist position / smart-list rank (-1 in Library)
    QVector<int> m_basePositions;    // after search: positions into m_sourceRows
    int m_librarySortColumn = 4;     // TrackModel::ColArtist
    Qt::SortOrder m_librarySortOrder = Qt::AscendingOrder;
    QString m_snapshotPath;
};
