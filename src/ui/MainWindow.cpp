// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainWindow.h"

#include "library/Library.h"
#include "library/TrackModel.h"
#include "ui/BrowserPanes.h"
#include "ui/LcdDisplay.h"
#include "ui/Theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QSettings>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QIcon icon(const char *name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name)));
}

QToolButton *transportButton(const char *iconName, const QString &tip, int iconPx, const char *objName, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QLatin1String(objName));
    b->setIcon(icon(iconName));
    b->setIconSize(QSize(iconPx, iconPx));
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setAutoRaise(false);
    return b;
}

QTreeWidgetItem *sourceHeader(QTreeWidget *tree, const QString &text)
{
    auto *item = new QTreeWidgetItem(tree, {text});
    item->setFlags(Qt::ItemIsEnabled);
    QFont f = tree->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    item->setFont(0, f);
    item->setForeground(0, QColor(21, 244, 238, 150));
    item->setExpanded(true);
    return item;
}

} // namespace

MainWindow::MainWindow(Library *library, QWidget *parent)
    : QMainWindow(parent), m_library(library), m_engine(new GstEngine(this))
{
    setWindowTitle(QStringLiteral("CyanBeat"));
    setWindowIcon(icon("cyanbeat"));
    resize(1280, 800);

    m_model = new TrackModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(TrackModel::SortRole);
    m_proxy->setSortLocaleAware(true);

    buildMenus();

    auto *central = new QWidget(this);
    auto *col = new QVBoxLayout(central);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(buildTopBar());

    m_mainSplit = new QSplitter(Qt::Horizontal, central);
    m_mainSplit->setChildrenCollapsible(false);
    m_mainSplit->addWidget(buildSourceList());

    m_contentSplit = new QSplitter(Qt::Vertical, m_mainSplit);
    m_contentSplit->setChildrenCollapsible(false);
    m_browser = new BrowserPanes(m_contentSplit);
    m_browser->setMinimumHeight(90);

    m_table = new QTableView(m_contentSplit);
    m_table->setModel(m_proxy);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(TrackModel::ColArtist, Qt::AscendingOrder);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 6);
    m_table->setIconSize(QSize(14, 14));
    QHeaderView *hh = m_table->horizontalHeader();
    hh->setHighlightSections(false);
    hh->setSectionsMovable(true);
    hh->setStretchLastSection(false);
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setSectionResizeMode(TrackModel::ColPlaying, QHeaderView::Fixed);
    hh->resizeSection(TrackModel::ColPlaying, 24);
    hh->resizeSection(TrackModel::ColTitle, 380);
    hh->resizeSection(TrackModel::ColTime, 76);
    hh->resizeSection(TrackModel::ColArtist, 210);
    hh->resizeSection(TrackModel::ColAlbum, 230);
    hh->resizeSection(TrackModel::ColGenre, 130);
    hh->resizeSection(TrackModel::ColYear, 60);

    m_contentSplit->addWidget(m_browser);
    m_contentSplit->addWidget(m_table);
    m_contentSplit->setStretchFactor(0, 0);
    m_contentSplit->setStretchFactor(1, 1);
    m_contentSplit->setSizes({170, 600});
    m_mainSplit->addWidget(m_contentSplit);
    m_mainSplit->setStretchFactor(0, 0);
    m_mainSplit->setStretchFactor(1, 1);
    m_mainSplit->setSizes({210, 1070});

    col->addWidget(m_mainSplit, 1);
    col->addWidget(buildStatusStrip());
    setCentralWidget(central);

    // Wiring.
    connect(m_table, &QTableView::activated, this, [this](const QModelIndex &idx) { playFromRow(idx.row()); });
    connect(m_browser, &BrowserPanes::filterChanged, this, &MainWindow::onBrowserFiltered);
    connect(m_library, &Library::tracksAboutToChange, this, [this] {
        m_queuePaths.clear();
        for (int li : std::as_const(m_queue))
            m_queuePaths << m_library->tracks()[li].path;
    });
    connect(m_library, &Library::tracksChanged, this, &MainWindow::onLibraryChanged);
    connect(m_library, &Library::scanStarted, this, [this] {
        m_summary->setText(tr("Scanning library…"));
        if (m_queuePos < 0)
            m_lcd->setStatusText(tr("Scanning library…"));
    });
    connect(m_library, &Library::scanFinished, this, [this] {
        if (m_queuePos < 0)
            m_lcd->setStatusText(QString());
        updateSummary();
    });
    connect(m_lcd, &LcdDisplay::seekRequested, m_engine, &GstEngine::seek);
    connect(m_engine, &GstEngine::endOfStream, this, &MainWindow::onEndOfStream);
    connect(m_engine, &GstEngine::error, this, &MainWindow::onEngineError);
    connect(m_engine, &GstEngine::stateChanged, this, &MainWindow::onEngineState);

    m_tick = new QTimer(this);
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, [this] {
        m_lcd->setPosition(m_engine->positionMs(), m_engine->durationMs());
    });

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(120);
    connect(m_searchDebounce, &QTimer::timeout, this, &MainWindow::refilter);
    connect(m_search, &QLineEdit::textChanged, m_searchDebounce, qOverload<>(&QTimer::start));

    loadSettings();

    if (!m_engine->isValid())
        QTimer::singleShot(0, this, [this] {
            QMessageBox::critical(this, tr("CyanBeat"),
                                  tr("Audio playback is unavailable:\n%1").arg(m_engine->initError()));
        });

    refilter();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- building

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Add Folder to Library…"), QKeySequence::Open, this, [this] {
        const QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder to Library"), start);
        if (!dir.isEmpty())
            m_library->addFolder(dir);
    });
    QMenu *remove = file->addMenu(tr("&Remove Folder from Library"));
    connect(remove, &QMenu::aboutToShow, this, [this, remove] {
        remove->clear();
        const QStringList folders = m_library->folders();
        if (folders.isEmpty())
            remove->addAction(tr("(no folders)"))->setEnabled(false);
        for (const QString &f : folders)
            remove->addAction(f, this, [this, f] { m_library->removeFolder(f); });
    });
    file->addAction(tr("Re&scan Library"), QKeySequence(Qt::CTRL | Qt::Key_R), m_library, &Library::rescan);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *controls = menuBar()->addMenu(tr("&Controls"));
    controls->addAction(tr("Play / Pause"), QKeySequence(Qt::Key_Space), this, &MainWindow::togglePlay);
    controls->addAction(tr("Stop"), QKeySequence(Qt::CTRL | Qt::Key_Period), this, &MainWindow::stop);
    controls->addAction(tr("Next Song"), QKeySequence(Qt::CTRL | Qt::Key_Right), this, [this] { next(true); });
    controls->addAction(tr("Previous Song"), QKeySequence(Qt::CTRL | Qt::Key_Left), this, &MainWindow::previous);
    controls->addSeparator();
    controls->addAction(tr("Volume Up"), QKeySequence(Qt::CTRL | Qt::Key_Up), this,
                        [this] { m_volume->setValue(m_volume->value() + 5); });
    controls->addAction(tr("Volume Down"), QKeySequence(Qt::CTRL | Qt::Key_Down), this,
                        [this] { m_volume->setValue(m_volume->value() - 5); });
    controls->addSeparator();
    m_shuffleAction = controls->addAction(tr("Shuffle"));
    m_shuffleAction->setCheckable(true);
    connect(m_shuffleAction, &QAction::toggled, this, &MainWindow::setShuffle);
    controls->addAction(tr("Cycle Repeat (Off / All / One)"), this, &MainWindow::cycleRepeat);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_browserAction = view->addAction(tr("Show Column &Browser"));
    m_browserAction->setCheckable(true);
    m_browserAction->setChecked(true);
    m_browserAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(m_browserAction, &QAction::toggled, this, [this](bool on) {
        if (!on)
            m_browser->clearSelection();
        m_browser->setVisible(on);
    });
    view->addAction(tr("Go to Current Song"), QKeySequence(Qt::CTRL | Qt::Key_L), this, &MainWindow::revealPlaying);
    view->addAction(tr("Search"), QKeySequence::Find, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("TopBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(14, 8, 14, 8);
    h->setSpacing(8);

    auto *prev = transportButton("prev", tr("Previous"), 18, "Transport", bar);
    m_playBtn = transportButton("play", tr("Play"), 24, "PlayButton", bar);
    auto *nextBtn = transportButton("next", tr("Next"), 18, "Transport", bar);
    connect(prev, &QToolButton::clicked, this, &MainWindow::previous);
    connect(m_playBtn, &QToolButton::clicked, this, &MainWindow::togglePlay);
    connect(nextBtn, &QToolButton::clicked, this, [this] { next(true); });
    h->addWidget(prev, 0, Qt::AlignVCenter);
    h->addWidget(m_playBtn, 0, Qt::AlignVCenter);
    h->addWidget(nextBtn, 0, Qt::AlignVCenter);
    h->addSpacing(14);

    auto *volLow = new QLabel(bar);
    volLow->setPixmap(icon("vol_low").pixmap(14, 14));
    auto *volHigh = new QLabel(bar);
    volHigh->setPixmap(icon("vol_high").pixmap(16, 16));
    m_volume = new QSlider(Qt::Horizontal, bar);
    m_volume->setRange(0, 100);
    m_volume->setFixedWidth(110);
    m_volume->setFocusPolicy(Qt::NoFocus);
    m_volume->setToolTip(tr("Volume"));
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { m_engine->setVolume(v / 100.0); });
    h->addWidget(volLow);
    h->addWidget(m_volume);
    h->addWidget(volHigh);
    h->addSpacing(18);

    m_lcd = new LcdDisplay(bar);
    h->addWidget(m_lcd, 1);
    h->addSpacing(18);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(icon("search"), QLineEdit::LeadingPosition);
    m_search->setFixedWidth(220);
    h->addWidget(m_search, 0, Qt::AlignVCenter);
    return bar;
}

QWidget *MainWindow::buildSourceList()
{
    m_sources = new QTreeWidget(this);
    m_sources->setObjectName(QStringLiteral("SourceList"));
    m_sources->setHeaderHidden(true);
    m_sources->setRootIsDecorated(false);
    m_sources->setIndentation(10);
    m_sources->setIconSize(QSize(16, 16));
    m_sources->setMinimumWidth(170);
    m_sources->setFocusPolicy(Qt::NoFocus);

    auto *lib = sourceHeader(m_sources, tr("LIBRARY"));
    auto *music = new QTreeWidgetItem(lib, {tr("Music")});
    music->setIcon(0, icon("music"));

    auto *devices = sourceHeader(m_sources, tr("DEVICES"));
    auto *none = new QTreeWidgetItem(devices, {tr("No phone connected")});
    none->setIcon(0, icon("phone"));
    none->setFlags(Qt::NoItemFlags);

    auto *playlists = sourceHeader(m_sources, tr("PLAYLISTS"));
    auto *empty = new QTreeWidgetItem(playlists, {tr("No playlists yet")});
    empty->setIcon(0, icon("playlist"));
    empty->setFlags(Qt::NoItemFlags);

    m_sources->expandAll();
    m_sources->setCurrentItem(music);
    return m_sources;
}

QWidget *MainWindow::buildStatusStrip()
{
    auto *strip = new QWidget(this);
    strip->setObjectName(QStringLiteral("StatusStrip"));
    strip->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(strip);
    h->setContentsMargins(8, 4, 8, 4);
    h->setSpacing(6);

    auto stripButton = [strip](const char *iconName, const QString &tip) {
        auto *b = new QToolButton(strip);
        b->setObjectName(QStringLiteral("StripButton"));
        b->setIcon(icon(iconName));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    auto *add = stripButton("add", tr("Add a folder to the library"));
    connect(add, &QToolButton::clicked, this, [this] {
        const QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder to Library"), start);
        if (!dir.isEmpty())
            m_library->addFolder(dir);
    });
    m_shuffleBtn = stripButton("shuffle", tr("Shuffle"));
    m_shuffleBtn->setCheckable(true);
    connect(m_shuffleBtn, &QToolButton::toggled, m_shuffleAction, &QAction::setChecked);
    m_repeatBtn = stripButton("repeat", tr("Repeat: off"));
    m_repeatBtn->setCheckable(true);
    connect(m_repeatBtn, &QToolButton::clicked, this, &MainWindow::cycleRepeat);

    h->addWidget(add);
    h->addSpacing(6);
    h->addWidget(m_shuffleBtn);
    h->addWidget(m_repeatBtn);

    m_summary = new QLabel(strip);
    m_summary->setObjectName(QStringLiteral("Summary"));
    m_summary->setAlignment(Qt::AlignCenter);
    h->addWidget(m_summary, 1);
    // Balance the left buttons so the summary sits dead centre.
    h->addSpacing(3 * 30 + 6);
    return strip;
}

// ---------------------------------------------------------------- filtering

void MainWindow::refilter()
{
    const TrackList &tracks = m_library->tracks();
    const QStringList words = m_search->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVector<int> base;
    base.reserve(tracks.size());
    for (int i = 0; i < tracks.size(); ++i) {
        const Track &t = tracks[i];
        bool ok = true;
        for (const QString &w : words) {
            if (!t.title.contains(w, Qt::CaseInsensitive) && !t.artist.contains(w, Qt::CaseInsensitive)
                && !t.album.contains(w, Qt::CaseInsensitive) && !t.albumArtist.contains(w, Qt::CaseInsensitive)
                && !t.genre.contains(w, Qt::CaseInsensitive)) {
                ok = false;
                break;
            }
        }
        if (ok)
            base << i;
    }
    m_browser->setSource(&tracks, std::move(base));  // → onBrowserFiltered
}

void MainWindow::onBrowserFiltered()
{
    m_model->setSource(&m_library->tracks(), m_browser->filtered());
    m_model->setPlaying(currentLibraryIndex(), m_engine->state() == GstEngine::State::Paused);
    updateSummary();
}

void MainWindow::updateSummary()
{
    if (m_library->isScanning() && m_library->tracks().isEmpty()) {
        m_summary->setText(tr("Scanning library…"));
        return;
    }
    const TrackList &tracks = m_library->tracks();
    qint64 ms = 0, bytes = 0;
    for (int i : m_model->rows()) {
        ms += tracks[i].durationMs;
        bytes += tracks[i].sizeBytes;
    }
    if (tracks.isEmpty() && m_library->folders().isEmpty())
        m_summary->setText(tr("Add a music folder with + to get started"));
    else
        m_summary->setText(formatLibrarySummary(int(m_model->rows().size()), ms, bytes));
}

void MainWindow::onLibraryChanged()
{
    // Indices moved; re-anchor the queue by path.
    const TrackList &tracks = m_library->tracks();
    QHash<QString, int> byPath;
    byPath.reserve(tracks.size());
    for (int i = 0; i < tracks.size(); ++i)
        byPath.insert(tracks[i].path, i);

    m_queue.clear();
    m_queuePos = -1;
    for (const QString &p : std::as_const(m_queuePaths)) {
        const auto it = byPath.constFind(p);
        if (it == byPath.cend())
            continue;
        if (p == m_currentPath)
            m_queuePos = int(m_queue.size());
        m_queue << *it;
    }
    m_queuePaths.clear();

    if (m_queuePos < 0)
        m_lcd->setStatusText(QString());
    refilter();

    if (!m_snapshotPath.isEmpty()) {
        const QString path = m_snapshotPath;
        m_snapshotPath.clear();
        resize(1280, 800);
        // CYANBEAT_SNAPSHOT_PLAY=1 starts the first song so the display shows a track.
        const bool play = qEnvironmentVariableIsSet("CYANBEAT_SNAPSHOT_PLAY");
        if (play)
            QTimer::singleShot(100, this, [this] { playFromRow(0); });
        QTimer::singleShot(play ? 2500 : 400, this, [this, path] {
            grab().save(path);
            QApplication::quit();
        });
    }
}

// ---------------------------------------------------------------- playback

int MainWindow::currentLibraryIndex() const
{
    return (m_queuePos >= 0 && m_queuePos < m_queue.size()) ? m_queue[m_queuePos] : -1;
}

void MainWindow::playFromRow(int proxyRow)
{
    const int rows = m_proxy->rowCount();
    if (proxyRow < 0 || proxyRow >= rows)
        return;
    m_queue.clear();
    m_queue.reserve(rows);
    for (int r = 0; r < rows; ++r)
        m_queue << m_proxy->index(r, 0).data(TrackModel::IndexRole).toInt();
    int pos = proxyRow;
    if (m_shuffle) {
        const int chosen = m_queue[proxyRow];
        m_queue.removeAt(proxyRow);
        std::shuffle(m_queue.begin(), m_queue.end(), *QRandomGenerator::global());
        m_queue.prepend(chosen);
        pos = 0;
    }
    m_errorStreak = 0;
    playQueueIndex(pos);
}

void MainWindow::playQueueIndex(int pos)
{
    if (pos < 0 || pos >= m_queue.size())
        return;
    m_queuePos = pos;
    const Track &t = m_library->tracks()[m_queue[pos]];
    m_currentPath = t.path;
    m_lcd->setTrack(t.title, t.artist, t.album);
    m_lcd->setPosition(0, t.durationMs);
    m_engine->playFile(t.path);
    m_model->setPlaying(m_queue[pos], false);
    setWindowTitle(QStringLiteral("%1 — CyanBeat").arg(t.title));
    m_tick->start();
}

void MainWindow::togglePlay()
{
    if (m_engine->state() != GstEngine::State::Stopped) {
        m_engine->togglePause();
        return;
    }
    if (currentLibraryIndex() >= 0) {
        playQueueIndex(m_queuePos);
        return;
    }
    // Nothing queued yet: start from the selection, else the top of the list.
    const QModelIndexList sel = m_table->selectionModel()->selectedRows();
    playFromRow(sel.isEmpty() ? 0 : sel.first().row());
}

void MainWindow::stop()
{
    m_engine->stop();
    m_tick->stop();
    m_lcd->setPosition(0, m_engine->durationMs());
}

void MainWindow::next(bool userInitiated)
{
    if (m_queue.isEmpty())
        return;
    if (!userInitiated && m_repeat == Repeat::One) {
        playQueueIndex(m_queuePos);
        return;
    }
    int pos = m_queuePos + 1;
    if (pos >= m_queue.size()) {
        if (m_repeat == Repeat::Off && !userInitiated) {
            stop();
            m_queuePos = 0;
            return;
        }
        if (m_shuffle)
            std::shuffle(m_queue.begin(), m_queue.end(), *QRandomGenerator::global());
        pos = 0;
    }
    playQueueIndex(pos);
}

void MainWindow::previous()
{
    if (m_queue.isEmpty())
        return;
    if (m_engine->positionMs() > 3000 || m_queuePos == 0) {
        m_engine->seek(0);
        return;
    }
    playQueueIndex(m_queuePos - 1);
}

void MainWindow::onEndOfStream()
{
    m_errorStreak = 0;
    next(false);
}

void MainWindow::onEngineError(const QString &msg)
{
    const QString name = m_currentPath.section(QLatin1Char('/'), -1);
    m_summary->setText(tr("Skipped \"%1\": %2").arg(name, msg));
    // Auto-skip unplayable files, but don't spin through a whole broken library.
    if (++m_errorStreak <= 5)
        QTimer::singleShot(150, this, [this] { next(false); });
}

void MainWindow::onEngineState(GstEngine::State s)
{
    const bool playing = s == GstEngine::State::Playing;
    m_playBtn->setIcon(icon(playing ? "pause" : "play"));
    m_playBtn->setToolTip(playing ? tr("Pause") : tr("Play"));
    m_model->setPlaying(s == GstEngine::State::Stopped && m_queuePos < 0 ? -1 : currentLibraryIndex(),
                        s == GstEngine::State::Paused);
    if (playing)
        m_tick->start();
}

void MainWindow::revealPlaying()
{
    const int li = currentLibraryIndex();
    if (li < 0)
        return;
    int row = m_model->rows().indexOf(li);
    if (row < 0) {
        // Not in the current filter: drop the filters and look again.
        m_search->clear();
        refilter();
        m_browser->clearSelection();
        row = m_model->rows().indexOf(li);
    }
    if (row < 0)
        return;
    const QModelIndex idx = m_proxy->mapFromSource(m_model->index(row, TrackModel::ColTitle));
    m_table->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    m_table->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void MainWindow::setShuffle(bool on)
{
    m_shuffle = on;
    const QSignalBlocker b1(m_shuffleBtn), b2(m_shuffleAction);
    m_shuffleBtn->setChecked(on);
    m_shuffleAction->setChecked(on);
    // Reorder what's left of the queue so the switch takes effect immediately.
    if (m_queuePos >= 0 && m_queuePos + 1 < m_queue.size() && on)
        std::shuffle(m_queue.begin() + m_queuePos + 1, m_queue.end(), *QRandomGenerator::global());
}

void MainWindow::cycleRepeat()
{
    m_repeat = m_repeat == Repeat::Off ? Repeat::All : m_repeat == Repeat::All ? Repeat::One : Repeat::Off;
    m_repeatBtn->setChecked(m_repeat != Repeat::Off);
    m_repeatBtn->setIcon(icon(m_repeat == Repeat::One ? "repeat_one" : "repeat"));
    m_repeatBtn->setToolTip(m_repeat == Repeat::Off ? tr("Repeat: off")
                            : m_repeat == Repeat::All ? tr("Repeat: all") : tr("Repeat: one"));
}

// ---------------------------------------------------------------- settings

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    m_mainSplit->restoreState(s.value(QStringLiteral("window/mainSplit")).toByteArray());
    m_contentSplit->restoreState(s.value(QStringLiteral("window/contentSplit")).toByteArray());
    m_table->horizontalHeader()->restoreState(s.value(QStringLiteral("window/trackHeader")).toByteArray());
    m_browserAction->setChecked(s.value(QStringLiteral("window/browser"), true).toBool());
    m_volume->setValue(s.value(QStringLiteral("playback/volume"), 80).toInt());
    m_engine->setVolume(m_volume->value() / 100.0);
    setShuffle(s.value(QStringLiteral("playback/shuffle"), false).toBool());
    const int rep = s.value(QStringLiteral("playback/repeat"), 0).toInt();
    for (int i = 0; i < rep % 3; ++i)
        cycleRepeat();
}

void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/mainSplit"), m_mainSplit->saveState());
    s.setValue(QStringLiteral("window/contentSplit"), m_contentSplit->saveState());
    s.setValue(QStringLiteral("window/trackHeader"), m_table->horizontalHeader()->saveState());
    s.setValue(QStringLiteral("window/browser"), m_browserAction->isChecked());
    s.setValue(QStringLiteral("playback/volume"), m_volume->value());
    s.setValue(QStringLiteral("playback/shuffle"), m_shuffle);
    s.setValue(QStringLiteral("playback/repeat"), int(m_repeat));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    m_engine->stop();
    QMainWindow::closeEvent(e);
}
