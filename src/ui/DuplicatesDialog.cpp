// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/DuplicatesDialog.h"

#include "engine/Player.h"
#include "library/Library.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {
enum Col { ColFile, ColAction, ColQuality, ColLength, ColSize, ColPlays, ColCount };
constexpr int IndexRole = Qt::UserRole + 1;   // index into m_data.tracks
constexpr int ScoreRole = Qt::UserRole + 2;
constexpr int GroupRole = Qt::UserRole + 3;   // group number on child items

QString reasonText(int reasons)
{
    QStringList why;
    if (reasons & Duplicates::SameBytes)
        why << QObject::tr("identical files");
    if (reasons & Duplicates::SameVideo)
        why << QObject::tr("same YouTube video");
    if (reasons & Duplicates::SameSong)
        why << QObject::tr("same song and length");
    return why.join(QStringLiteral(", "));
}

// Identical bytes or the same source video: no real doubt.
bool certain(int reasons)
{
    return reasons & (Duplicates::SameBytes | Duplicates::SameVideo);
}
}

DuplicatesDialog::DuplicatesDialog(Library *library, Player *player, QWidget *parent)
    : QDialog(parent), m_library(library), m_player(player)
{
    setWindowTitle(tr("Find Duplicates"));
    resize(1100, 680);
    auto *v = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Songs stored more than once. The checked copies will be moved to the Trash; the one left "
           "unchecked in each group stays. Plays and ratings move to the copy you keep, playlists follow it, "
           "and phone sync won't bring the removed files back. Double-click a file to listen."),
        this);
    intro->setWordWrap(true);
    v->addWidget(intro);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Keep:"), this));
    m_prefer = new QComboBox(this);
    m_prefer->addItem(tr("Best sound quality"));
    m_prefer->addItem(tr("Smallest file"));
    row->addWidget(m_prefer);
    row->addSpacing(16);
    m_includeSongs = new QCheckBox(tr("Include same-song matches (title and length — check these by ear)"), this);
    m_includeSongs->setChecked(true);
    row->addWidget(m_includeSongs);
    row->addStretch(1);
    m_status = new QLabel(this);
    row->addWidget(m_status);
    m_progress = new QProgressBar(this);
    m_progress->setFixedWidth(180);
    m_progress->setTextVisible(false);
    row->addWidget(m_progress);
    v->addLayout(row);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("File"), tr("Action"), tr("Sound"), tr("Length"), tr("Size"), tr("Plays")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    m_tree->header()->resizeSection(ColAction, 80);
    m_tree->header()->resizeSection(ColQuality, 300);
    m_tree->header()->resizeSection(ColLength, 70);
    m_tree->header()->resizeSection(ColSize, 80);
    m_tree->header()->resizeSection(ColPlays, 56);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(m_tree, 1);

    auto *bottom = new QHBoxLayout;
    m_summary = new QLabel(this);
    bottom->addWidget(m_summary, 1);
    m_rescan = new QPushButton(tr("Scan Again"), this);
    m_trash = new QPushButton(tr("Move Checked to Trash…"), this);
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_rescan);
    bottom->addWidget(m_trash);
    bottom->addWidget(close);
    v->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_rescan, &QPushButton::clicked, this, &DuplicatesDialog::startScan);
    connect(m_trash, &QPushButton::clicked, this, &DuplicatesDialog::moveToTrash);
    connect(m_prefer, &QComboBox::currentIndexChanged, this, &DuplicatesDialog::applyPreference);
    connect(m_includeSongs, &QCheckBox::toggled, this, &DuplicatesDialog::populate);
    connect(m_tree, &QTreeWidget::itemChanged, this, &DuplicatesDialog::onItemChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const int li = m_library->indexOfPath(memberPath(item));
        if (li >= 0)
            m_player->playQueue({li}, 0);
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QString path = memberPath(m_tree->itemAt(pos));
        if (path.isEmpty())
            return;
        QMenu menu(this);
        menu.addAction(tr("Play"), this, [this, path] {
            const int li = m_library->indexOfPath(path);
            if (li >= 0)
                m_player->playQueue({li}, 0);
        });
        menu.addAction(tr("Show in Folder"), this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path.section(QLatin1Char('/'), 0, -2)));
        });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });
    connect(&m_watcher, &QFutureWatcher<Scanned>::finished, this, [this] {
        if (m_cancel && m_cancel->load())
            return;
        m_data = m_watcher.result();
        m_progress->hide();
        m_rescan->setEnabled(true);
        m_prefer->setEnabled(true);
        m_includeSongs->setEnabled(true);
        applyPreference();
    });

    startScan();
}

DuplicatesDialog::~DuplicatesDialog()
{
    if (m_cancel)
        m_cancel->store(true);
    m_watcher.waitForFinished();
}

void DuplicatesDialog::startScan()
{
    if (m_watcher.isRunning())
        return;
    m_tree->clear();
    m_trash->setEnabled(false);
    m_rescan->setEnabled(false);
    m_prefer->setEnabled(false);
    m_includeSongs->setEnabled(false);
    m_progress->show();
    m_progress->setRange(0, 0);
    m_status->setText(tr("Looking for duplicates…"));
    m_summary->clear();

    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    const TrackList tracks = m_library->tracks();
    const auto prefer = m_prefer->currentIndex() == 1 ? Duplicates::Prefer::SmallestFile
                                                      : Duplicates::Prefer::BestQuality;
    QPointer<DuplicatesDialog> self(this);
    m_watcher.setFuture(QtConcurrent::run([tracks, prefer, cancel, self] {
        Scanned out;
        out.tracks = tracks;
        out.groups = Duplicates::scan(tracks, prefer, cancel.get(), [self, cancel](int done, int total) {
            if (cancel->load())
                return;
            QMetaObject::invokeMethod(qApp, [self, done, total] {
                if (!self)
                    return;
                self->m_progress->setRange(0, total);
                self->m_progress->setValue(done);
                self->m_status->setText(tr("Comparing copies… %1 / %2").arg(done).arg(total));
            });
        });
        return out;
    }));
}

void DuplicatesDialog::applyPreference()
{
    const auto prefer = m_prefer->currentIndex() == 1 ? Duplicates::Prefer::SmallestFile
                                                      : Duplicates::Prefer::BestQuality;
    Duplicates::rescore(m_data.groups, m_data.tracks, prefer);
    populate();
}

QString DuplicatesDialog::memberPath(const QTreeWidgetItem *item) const
{
    if (!item || !item->data(ColFile, IndexRole).isValid())
        return {};
    const int i = item->data(ColFile, IndexRole).toInt();
    return (i >= 0 && i < m_data.tracks.size()) ? m_data.tracks[i].path : QString();
}

void DuplicatesDialog::populate()
{
    m_populating = true;
    m_tree->clear();
    const QLocale loc;
    QFont bold = m_tree->font();
    bold.setBold(true);
    int shown = 0;
    for (int gi = 0; gi < m_data.groups.size(); ++gi) {
        const Duplicates::Group &g = m_data.groups[gi];
        if (!m_includeSongs->isChecked() && !certain(g.reasons))
            continue;
        // Skip groups whose files changed or vanished since the scan.
        bool stale = false;
        for (const auto &m : g.members)
            stale = stale || m_library->indexOfPath(m_data.tracks[m.index].path) < 0
                    || !QFileInfo::exists(m_data.tracks[m.index].path);
        if (stale)
            continue;
        ++shown;

        auto *head = new QTreeWidgetItem(m_tree);
        head->setText(ColFile, QStringLiteral("%1   —   %2 copies · %3")
                                   .arg(Duplicates::normalizedTitle(m_data.tracks[g.members[0].index]))
                                   .arg(g.members.size())
                                   .arg(reasonText(g.reasons)));
        head->setFirstColumnSpanned(true);
        head->setFont(ColFile, bold);
        head->setFlags(Qt::ItemIsEnabled);
        head->setForeground(ColFile, QColor(21, 244, 238, certain(g.reasons) ? 255 : 170));

        for (int k = 0; k < g.members.size(); ++k) {
            const Duplicates::Member &m = g.members[k];
            const Track &t = m_data.tracks[m.index];
            auto *item = new QTreeWidgetItem(m_tree);
            item->setText(ColFile, QStringLiteral("    ") + t.path.section(QLatin1Char('/'), -1));
            item->setToolTip(ColFile, t.path);
            item->setText(ColQuality, Duplicates::describe(m.quality));
            item->setText(ColLength, formatDuration(m.quality.durationMs > 0 ? m.quality.durationMs : t.durationMs));
            item->setText(ColSize, loc.formattedDataSize(t.sizeBytes, 1, QLocale::DataSizeTraditionalFormat));
            item->setText(ColPlays, t.playCount > 0 ? QString::number(t.playCount) : QString());
            item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
            item->setTextAlignment(ColPlays, Qt::AlignRight | Qt::AlignVCenter);
            item->setData(ColFile, IndexRole, m.index);
            item->setData(ColFile, ScoreRole, m.score);
            item->setData(ColFile, GroupRole, gi);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(ColFile, k == g.keep ? Qt::Unchecked : Qt::Checked);
            item->setText(ColAction, k == g.keep ? tr("keep") : tr("remove"));
            if (k == g.keep)
                item->setFont(ColAction, bold);
        }
    }
    m_populating = false;
    m_status->setText(shown ? tr("%1 groups").arg(shown) : tr("No duplicates found."));
    updateSummary();
}

void DuplicatesDialog::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_populating || column != ColFile || !item->data(ColFile, GroupRole).isValid())
        return;
    const int gi = item->data(ColFile, GroupRole).toInt();
    // Never let a whole group be checked: one copy always stays.
    int unchecked = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (it->data(ColFile, GroupRole).isValid() && it->data(ColFile, GroupRole).toInt() == gi
            && it->checkState(ColFile) == Qt::Unchecked)
            ++unchecked;
    }
    m_populating = true;
    if (unchecked == 0) {
        item->setCheckState(ColFile, Qt::Unchecked);
        m_status->setText(tr("One copy of each song always stays."));
    }
    QFont normal = m_tree->font(), bold = normal;
    bold.setBold(true);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (!it->data(ColFile, GroupRole).isValid() || it->data(ColFile, GroupRole).toInt() != gi)
            continue;
        const bool keep = it->checkState(ColFile) == Qt::Unchecked;
        it->setText(ColAction, keep ? tr("keep") : tr("remove"));
        it->setFont(ColAction, keep ? bold : normal);
    }
    m_populating = false;
    updateSummary();
}

void DuplicatesDialog::updateSummary()
{
    int files = 0;
    qint64 bytes = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (it->data(ColFile, GroupRole).isValid() && it->checkState(ColFile) == Qt::Checked) {
            ++files;
            bytes += m_data.tracks[it->data(ColFile, IndexRole).toInt()].sizeBytes;
        }
    }
    m_trash->setEnabled(files > 0);
    m_summary->setText(files ? tr("%1 files checked · frees %2")
                                   .arg(files)
                                   .arg(QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat))
                             : QString());
}

void DuplicatesDialog::moveToTrash()
{
    // Per group: checked copies go, merging into the best-scoring copy left unchecked.
    QVector<QPair<QString, QString>> removeKeep;
    QHash<int, QVector<QTreeWidgetItem *>> byGroup;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (it->data(ColFile, GroupRole).isValid())
            byGroup[it->data(ColFile, GroupRole).toInt()] << it;
    }
    for (const auto &items : std::as_const(byGroup)) {
        QTreeWidgetItem *keeper = nullptr;
        for (QTreeWidgetItem *it : items)
            if (it->checkState(ColFile) == Qt::Unchecked
                && (!keeper || it->data(ColFile, ScoreRole).toDouble() > keeper->data(ColFile, ScoreRole).toDouble()))
                keeper = it;
        if (!keeper)
            continue;
        for (QTreeWidgetItem *it : items)
            if (it->checkState(ColFile) == Qt::Checked)
                removeKeep << qMakePair(memberPath(it), memberPath(keeper));
    }
    if (removeKeep.isEmpty())
        return;

    const auto answer = QMessageBox::question(
        this, tr("Move to Trash"),
        tr("Move %n duplicate file(s) to the Trash?\nYou can restore them from the Trash.", nullptr,
           int(removeKeep.size())),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    const QStringList failed = m_library->removeDuplicates(removeKeep);
    if (!failed.isEmpty()) {
        QMessageBox::warning(this, tr("Move to Trash"),
                             tr("These files couldn't be moved to the Trash and were left as they are:\n\n%1")
                                 .arg(failed.join(QLatin1Char('\n'))));
    }
    // Removed files drop out of the library, which makes their groups stale.
    populate();
    m_status->setText(tr("Moved %1 files to the Trash.").arg(removeKeep.size() - failed.size()));
}
