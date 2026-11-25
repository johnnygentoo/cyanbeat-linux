// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SyncDialog.h"

#include "library/Library.h"
#include "sync/Devices.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int FolderRole = Qt::UserRole + 1;  // folder index in the plan
constexpr int ItemRole = Qt::UserRole + 2;    // item index in that folder
constexpr int ActionRole = Qt::UserRole + 3;

QString bytesText(qint64 b)
{
    return QLocale().formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat);
}

QString heading(SyncPlan::Action a)
{
    switch (a) {
    case SyncPlan::Action::Push: return QObject::tr("Copy to phone");
    case SyncPlan::Action::Pull: return QObject::tr("Copy to this PC");
    case SyncPlan::Action::DeleteRemote: return QObject::tr("Delete from phone");
    case SyncPlan::Action::DeleteLocal: return QObject::tr("Move to Trash on this PC");
    case SyncPlan::Action::Conflict: return QObject::tr("Same name, different file — left alone");
    }
    return {};
}
}

SyncDialog::SyncDialog(Library *library, Devices *devices, QWidget *parent)
    : QDialog(parent), m_library(library), m_devices(devices), m_session(new SyncSession(library, this))
{
    setWindowTitle(tr("Sync Phone"));
    resize(1000, 720);
    auto *v = new QVBoxLayout(this);

    // Phone.
    auto *phoneRow = new QHBoxLayout;
    phoneRow->addWidget(new QLabel(tr("Phone:"), this));
    m_phone = new QComboBox(this);
    m_phone->setMinimumWidth(260);
    phoneRow->addWidget(m_phone);
    m_phoneState = new QLabel(this);
    phoneRow->addWidget(m_phoneState, 1);
    m_refresh = new QPushButton(tr("Refresh"), this);
    m_wifi = new QPushButton(tr("Connect over Wi-Fi"), this);
    m_setupWifi = new QPushButton(tr("Set Up Wi-Fi Sync"), this);
    m_setupWifi->setToolTip(tr("While the phone is plugged in: let it sync over Wi-Fi until it restarts.\n"
                               "For a permanent setup, turn on Developer options › Wireless debugging."));
    phoneRow->addWidget(m_refresh);
    phoneRow->addWidget(m_wifi);
    phoneRow->addWidget(m_setupWifi);
    v->addLayout(phoneRow);

    // Folders.
    auto *folderLabel = new QLabel(tr("Folders to sync (this PC ↔ phone):"), this);
    v->addWidget(folderLabel);
    m_folders = new QTableWidget(0, 2, this);
    m_folders->setHorizontalHeaderLabels({tr("On this PC"), tr("On the phone")});
    m_folders->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_folders->verticalHeader()->hide();
    m_folders->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_folders->setMaximumHeight(110);
    v->addWidget(m_folders);
    auto *folderButtons = new QHBoxLayout;
    auto *addFolder = new QPushButton(tr("Add Folder…"), this);
    auto *removeFolder = new QPushButton(tr("Remove"), this);
    folderButtons->addWidget(addFolder);
    folderButtons->addWidget(removeFolder);
    folderButtons->addSpacing(24);
    m_merge = new QRadioButton(tr("Merge — new songs go both ways, nothing is deleted"), this);
    m_mirror = new QRadioButton(tr("Mirror — also carry deletions across (asks first)"), this);
    (QSettings().value(QStringLiteral("sync/mirror"), false).toBool() ? m_mirror : m_merge)->setChecked(true);
    folderButtons->addWidget(m_merge);
    folderButtons->addWidget(m_mirror);
    folderButtons->addStretch(1);
    v->addLayout(folderButtons);

    // Plan.
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("File"), tr("Size"), tr("Why")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 120);
    m_tree->header()->resizeSection(2, 330);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setUniformRowHeights(true);
    v->addWidget(m_tree, 1);

    auto *bottom = new QHBoxLayout;
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    bottom->addWidget(m_summary, 1);
    m_progress = new QProgressBar(this);
    m_progress->setFixedWidth(220);
    m_progress->hide();
    bottom->addWidget(m_progress);
    m_check = new QPushButton(tr("Check"), this);
    m_sync = new QPushButton(tr("Sync"), this);
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->hide();
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_check);
    bottom->addWidget(m_sync);
    bottom->addWidget(m_cancel);
    bottom->addWidget(close);
    v->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_refresh, &QPushButton::clicked, m_devices, &Devices::refresh);
    connect(m_wifi, &QPushButton::clicked, this, [this] { m_devices->connectWireless(currentPhoneId()); });
    connect(m_setupWifi, &QPushButton::clicked, this, [this] { m_devices->enableWireless(currentPhoneId()); });
    connect(m_devices, &Devices::changed, this, &SyncDialog::refreshPhones);
    connect(m_devices, &Devices::message, this, [this](const QString &t) {
        QMessageBox::information(this, tr("Sync Phone"), t);
    });
    connect(m_phone, &QComboBox::currentIndexChanged, this, [this] {
        updateButtons();
        if (currentPhoneId() != m_planPhone)
            m_tree->clear();
    });
    connect(addFolder, &QPushButton::clicked, this, [this] {
        const QString local = QFileDialog::getExistingDirectory(this, tr("Folder on this PC"));
        if (local.isEmpty())
            return;
        bool ok = false;
        const QString remote = QInputDialog::getText(this, tr("Folder on the phone"), tr("Phone folder:"),
                                                     QLineEdit::Normal,
                                                     QStringLiteral("/sdcard/Music/") + local.section(QLatin1Char('/'), -1),
                                                     &ok);
        if (!ok || remote.trimmed().isEmpty())
            return;
        auto list = SyncSession::folders();
        list.push_back({local, remote.trimmed(), true});
        SyncSession::setFolders(list);
        refreshFolders();
    });
    connect(removeFolder, &QPushButton::clicked, this, [this] {
        const int row = m_folders->currentRow();
        auto list = SyncSession::folders();
        if (row < 0 || row >= list.size())
            return;
        list.removeAt(row);
        SyncSession::setFolders(list);
        refreshFolders();
    });
    connect(m_folders, &QTableWidget::itemChanged, this, [this] {
        if (!m_filling)
            saveFolders();
    });
    connect(m_mirror, &QRadioButton::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("sync/mirror"), on);
        m_tree->clear();
        m_summary->clear();
        updateButtons();
    });
    connect(m_check, &QPushButton::clicked, this, &SyncDialog::check);
    connect(m_sync, &QPushButton::clicked, this, &SyncDialog::startSync);
    connect(m_cancel, &QPushButton::clicked, m_session, &SyncSession::cancel);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *, int) {
        if (!m_filling)
            updatePlanSummary();
    });
    connect(m_session, &SyncSession::status, m_summary, &QLabel::setText);
    connect(m_session, &SyncSession::planned, this, &SyncDialog::showPlan);
    connect(m_session, &SyncSession::progress, this,
            [this](int done, int total, qint64 bytes, qint64 totalBytes, const QString &current) {
                m_progress->setRange(0, qMax(1, total));
                m_progress->setValue(done);
                m_summary->setText(current.isEmpty() ? tr("Finishing…")
                                                     : tr("%1 of %2 · %3 of %4 · %5")
                                                           .arg(done + 1).arg(total)
                                                           .arg(bytesText(bytes), bytesText(totalBytes), current));
            });
    connect(m_session, &SyncSession::finished, this, &SyncDialog::onFinished);

    refreshFolders();
    refreshPhones();
    if (!m_devices->adbAvailable())
        m_summary->setText(tr("adb isn't installed. Install Android platform-tools (android-tools) to sync."));
}

QString SyncDialog::currentPhoneId() const
{
    return m_phone->currentData().toString();
}

QString SyncDialog::currentTransport() const
{
    const Devices::Phone *p = m_devices->phone(currentPhoneId());
    return p ? p->transport : QString();
}

void SyncDialog::selectPhone(const QString &id)
{
    const int i = m_phone->findData(id);
    if (i >= 0)
        m_phone->setCurrentIndex(i);
}

void SyncDialog::refreshPhones()
{
    const QString keep = currentPhoneId();
    const QSignalBlocker block(m_phone);
    m_phone->clear();
    for (const Devices::Phone &p : m_devices->phones()) {
        QString label = p.model.isEmpty() ? p.id : p.model;
        if (p.unauthorized)
            label += tr(" — tap \"Allow\" on the phone");
        else if (p.transport.isEmpty())
            label += tr(" — not connected");
        else
            label += p.wireless ? tr(" — Wi-Fi") : tr(" — USB");
        m_phone->addItem(label, p.id);
    }
    int idx = m_phone->findData(keep);
    if (idx < 0)  // prefer a connected phone
        for (int i = 0; i < m_devices->phones().size() && idx < 0; ++i)
            if (!m_devices->phones()[i].transport.isEmpty() && !m_devices->phones()[i].unauthorized)
                idx = i;
    m_phone->setCurrentIndex(qMax(0, idx));
    updateButtons();
}

void SyncDialog::refreshFolders()
{
    m_filling = true;
    const auto list = SyncSession::folders();
    m_folders->setRowCount(int(list.size()));
    for (int r = 0; r < list.size(); ++r) {
        auto *l = new QTableWidgetItem(list[r].local);
        l->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        l->setCheckState(list[r].enabled ? Qt::Checked : Qt::Unchecked);
        auto *rm = new QTableWidgetItem(list[r].remote);
        rm->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        m_folders->setItem(r, 0, l);
        m_folders->setItem(r, 1, rm);
    }
    m_filling = false;
}

void SyncDialog::saveFolders()
{
    QVector<SyncSession::Folder> list;
    for (int r = 0; r < m_folders->rowCount(); ++r)
        list.push_back({m_folders->item(r, 0)->text(), m_folders->item(r, 1)->text().trimmed(),
                        m_folders->item(r, 0)->checkState() == Qt::Checked});
    SyncSession::setFolders(list);
    m_tree->clear();
}

void SyncDialog::updateButtons()
{
    const Devices::Phone *p = m_devices->phone(currentPhoneId());
    const bool connected = p && !p->transport.isEmpty() && !p->unauthorized;
    const bool busy = m_session->busy();
    m_phoneState->setText(!p ? tr("No phone found — plug one in with USB debugging on.")
                          : p->unauthorized ? tr("Waiting for you to allow USB debugging on the phone.")
                          : connected ? (p->wireless ? tr("Connected over Wi-Fi") : tr("Connected by USB"))
                                      : tr("Not connected"));
    m_wifi->setVisible(p && !connected && !p->unauthorized);
    m_setupWifi->setVisible(p && connected && !p->wireless);
    m_check->setEnabled(connected && !busy);
    bool anything = false;
    for (const auto &fp : m_plan.folders)
        for (const auto &i : fp.items)
            anything = anything || (i.checked && i.action != SyncPlan::Action::Conflict);
    m_sync->setEnabled(connected && !busy && m_tree->topLevelItemCount() > 0 && anything
                       && m_planPhone == currentPhoneId());
}

void SyncDialog::check()
{
    m_tree->clear();
    m_plan = {};
    m_planPhone = currentPhoneId();
    m_check->setEnabled(false);
    m_sync->setEnabled(false);
    m_session->scan(currentTransport(), m_planPhone, m_mirror->isChecked() ? SyncPlan::Mode::Mirror : SyncPlan::Mode::Merge);
}

void SyncDialog::showPlan(const SyncSession::Plan &plan)
{
    m_plan = plan;
    m_filling = true;
    m_tree->clear();
    QFont bold = m_tree->font();
    bold.setBold(true);
    QStringList errors;
    if (!plan.error.isEmpty())
        errors << plan.error;
    for (const SyncPlan::Action a : {SyncPlan::Action::Push, SyncPlan::Action::Pull, SyncPlan::Action::DeleteRemote,
                                     SyncPlan::Action::DeleteLocal, SyncPlan::Action::Conflict}) {
        QTreeWidgetItem *group = nullptr;
        for (int f = 0; f < plan.folders.size(); ++f) {
            const auto &fp = plan.folders[f];
            for (int k = 0; k < fp.items.size(); ++k) {
                const SyncPlan::Item &i = fp.items[k];
                if (i.action != a)
                    continue;
                if (!group) {
                    group = new QTreeWidgetItem(m_tree);
                    group->setFont(0, bold);
                    group->setData(0, ActionRole, int(a));
                    if (a != SyncPlan::Action::Conflict)
                        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
                    else
                        group->setFlags(Qt::ItemIsEnabled);
                }
                auto *row = new QTreeWidgetItem(group);
                const bool renamed = i.remoteRel != i.localRel;
                row->setText(0, renamed && a == SyncPlan::Action::Push
                                    ? QStringLiteral("%1   →   %2").arg(i.localRel, i.remoteRel)
                                    : i.localRel);
                row->setToolTip(0, fp.folder.local + QLatin1Char('/') + i.localRel);
                row->setText(1, bytesText(i.size));
                row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
                row->setText(2, i.why);
                row->setData(0, FolderRole, f);
                row->setData(0, ItemRole, k);
                if (a == SyncPlan::Action::Conflict) {
                    row->setFlags(Qt::ItemIsEnabled);
                } else {
                    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                    row->setCheckState(0, i.checked ? Qt::Checked : Qt::Unchecked);
                }
            }
        }
    }
    for (const auto &fp : plan.folders)
        if (!fp.error.isEmpty())
            errors << fp.error;
    m_filling = false;
    updatePlanSummary();
    if (m_tree->topLevelItemCount() == 0 && errors.isEmpty())
        m_summary->setText(tr("Everything's already in step."));
    if (!errors.isEmpty())
        m_summary->setText(m_summary->text() + QStringLiteral("\n") + errors.join(QLatin1Char('\n')));
    updateButtons();
}

void SyncDialog::collectChecks()
{
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int c = 0; c < group->childCount(); ++c) {
            QTreeWidgetItem *row = group->child(c);
            const int f = row->data(0, FolderRole).toInt(), k = row->data(0, ItemRole).toInt();
            if (f < m_plan.folders.size() && k < m_plan.folders[f].items.size())
                m_plan.folders[f].items[k].checked = row->checkState(0) == Qt::Checked;
        }
    }
}

void SyncDialog::updatePlanSummary()
{
    collectChecks();
    QStringList parts;
    qint64 pushBytes = 0;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        const auto a = SyncPlan::Action(group->data(0, ActionRole).toInt());
        int n = 0, total = int(group->childCount());
        qint64 bytes = 0;
        for (int c = 0; c < total; ++c) {
            QTreeWidgetItem *row = group->child(c);
            const auto &item = m_plan.folders[row->data(0, FolderRole).toInt()].items[row->data(0, ItemRole).toInt()];
            if (a == SyncPlan::Action::Conflict || item.checked) {
                ++n;
                bytes += item.size;
            }
        }
        if (a == SyncPlan::Action::Push)
            pushBytes = bytes;
        const bool moves = a == SyncPlan::Action::Push || a == SyncPlan::Action::Pull;
        group->setText(0, QStringLiteral("%1 — %2 of %3").arg(heading(a)).arg(n).arg(total));
        group->setText(1, moves ? bytesText(bytes) : QString());
        if (n)
            parts << QStringLiteral("%1: %2%3").arg(heading(a)).arg(n).arg(moves ? QStringLiteral(" (%1)").arg(bytesText(bytes))
                                                                                  : QString());
    }
    QString text = parts.join(QStringLiteral(" · "));
    if (m_plan.freeOnPhone >= 0) {
        text += tr("\nFree on the phone: %1").arg(bytesText(m_plan.freeOnPhone));
        if (pushBytes > m_plan.freeOnPhone - 512LL * 1024 * 1024)
            text += tr(" — not enough room for everything checked; uncheck some.");
    }
    m_summary->setText(text);
    updateButtons();
    if (m_plan.freeOnPhone >= 0 && pushBytes > m_plan.freeOnPhone - 512LL * 1024 * 1024)
        m_sync->setEnabled(false);
}

void SyncDialog::startSync()
{
    collectChecks();
    int delRemote = 0, delLocal = 0;
    for (const auto &fp : m_plan.folders)
        for (const auto &i : fp.items)
            if (i.checked) {
                delRemote += i.action == SyncPlan::Action::DeleteRemote;
                delLocal += i.action == SyncPlan::Action::DeleteLocal;
            }
    if (delRemote || delLocal) {
        QStringList lines;
        if (delRemote)
            lines << tr("delete %n song(s) from the phone", nullptr, delRemote);
        if (delLocal)
            lines << tr("move %n song(s) on this PC to the Trash", nullptr, delLocal);
        if (QMessageBox::question(this, tr("Sync Phone"),
                                  tr("This sync will %1.\n\nGo ahead?").arg(lines.join(tr(" and "))),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            != QMessageBox::Yes)
            return;
    }
    m_check->setEnabled(false);
    m_sync->setEnabled(false);
    m_sync->hide();
    m_cancel->show();
    m_progress->show();
    m_tree->setEnabled(false);
    m_session->run(currentTransport(), m_planPhone, m_plan);
}

void SyncDialog::onFinished(const SyncSession::Report &r)
{
    m_progress->hide();
    m_cancel->hide();
    m_sync->show();
    m_tree->setEnabled(true);
    QStringList done;
    if (r.pushed)
        done << tr("%n copied to the phone", nullptr, r.pushed);
    if (r.pulled)
        done << tr("%n copied to this PC", nullptr, r.pulled);
    if (r.deletedRemote)
        done << tr("%n deleted from the phone", nullptr, r.deletedRemote);
    if (r.deletedLocal)
        done << tr("%n moved to the Trash here", nullptr, r.deletedLocal);
    QString text = (r.cancelled ? tr("Stopped. ") : tr("Done. ")) + (done.isEmpty() ? tr("Nothing changed.") : done.join(QStringLiteral(", ")) + QLatin1Char('.'));
    if (r.failed)
        text += tr(" %n failed.", nullptr, r.failed);
    m_tree->clear();
    m_summary->setText(text);
    updateButtons();
    if (!r.errors.isEmpty())
        QMessageBox::warning(this, tr("Sync Phone"), tr("Some files didn't sync:\n\n%1").arg(r.errors.mid(0, 30).join(QLatin1Char('\n'))));
}

void SyncDialog::closeEvent(QCloseEvent *e)
{
    if (m_session->busy()) {
        if (QMessageBox::question(this, tr("Sync Phone"), tr("A sync is running. Stop it?")) != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        m_session->cancel();
    }
    QDialog::closeEvent(e);
}
