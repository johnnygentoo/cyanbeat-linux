// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SyncSession.h"

#include "library/Library.h"
#include "library/Scanner.h"
#include "sync/Transport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

QVector<SyncSession::Folder> SyncSession::folders()
{
    QVector<Folder> out;
    const QByteArray json = QSettings().value(QStringLiteral("sync/folders")).toByteArray();
    if (json.isEmpty()) {
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!music.isEmpty())
            out.push_back({music, QStringLiteral("/sdcard/Music"), true});
        return out;
    }
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        out.push_back({o.value(QStringLiteral("local")).toString(), o.value(QStringLiteral("remote")).toString(),
                       o.value(QStringLiteral("enabled")).toBool(true)});
    }
    return out;
}

void SyncSession::setFolders(const QVector<Folder> &folders)
{
    QJsonArray arr;
    for (const Folder &f : folders)
        arr << QJsonObject{{QStringLiteral("local"), f.local}, {QStringLiteral("remote"), f.remote},
                           {QStringLiteral("enabled"), f.enabled}};
    QSettings().setValue(QStringLiteral("sync/folders"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

SyncSession::SyncSession(Library *library, QObject *parent) : QObject(parent), m_library(library)
{
    connect(&m_planWatch, &QFutureWatcher<Plan>::finished, this, [this] { Q_EMIT planned(m_planWatch.result()); });
    connect(&m_runWatch, &QFutureWatcher<Report>::finished, this, [this] {
        if (m_afterRun) {  // database + library book-keeping first, then tell the UI
            auto after = std::move(m_afterRun);
            m_afterRun = {};
            after();
        }
        Q_EMIT finished(m_runWatch.result());
    });
}

SyncSession::~SyncSession()
{
    cancel();
    m_planWatch.waitForFinished();
    m_runWatch.waitForFinished();
}

void SyncSession::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

namespace {

QString clean(QString p)
{
    p = QDir::cleanPath(p);
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p;
}

QVector<SyncPlan::File> listLocal(const QString &root)
{
    QVector<SyncPlan::File> out;
    const QDir base(root);
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = base.relativeFilePath(path);
        if (rel.startsWith(QLatin1Char('.')) || rel.contains(QLatin1String("/.")) || !Scanner::isAudioFile(path))
            continue;
        out.push_back({rel, it.fileInfo().size()});
    }
    return out;
}

} // namespace

void SyncSession::scan(const QString &transport, const QString &phoneId, SyncPlan::Mode mode)
{
    if (busy())
        return;
    // Gathered on this thread (the database belongs to it), used by the worker.
    struct Input {
        Folder folder;
        QSet<QString> synced, removed;
    };
    QVector<Input> inputs;
    QHash<QString, QVector<QString>> removedByRoot;
    const auto stones = m_library->tombstones();
    for (const Folder &f : folders()) {
        if (!f.enabled)
            continue;
        Input in;
        in.folder = {clean(f.local), clean(f.remote), true};
        in.synced = m_library->syncState(phoneId, in.folder.local);
        const QString prefix = in.folder.local + QLatin1Char('/');
        for (const Tombstone &t : stones)
            if (t.path.startsWith(prefix))
                in.removed.insert(t.path.mid(prefix.size()));
        inputs << in;
    }
    m_cancel = std::make_shared<std::atomic_bool>(false);
    QPointer<SyncSession> self(this);
    Q_EMIT status(tr("Reading the phone…"));
    m_planWatch.setFuture(QtConcurrent::run([inputs, transport, mode, self] {
        Plan plan;
        if (inputs.isEmpty()) {
            plan.error = tr("No folders are set to sync.");
            return plan;
        }
        const auto phone = Transport::open(transport);
        if (!phone || !phone->ok()) {
            plan.error = phone ? phone->error() : tr("This connection type isn't available in this build.");
            return plan;
        }
        for (const Input &in : inputs) {
            FolderPlan fp;
            fp.folder = in.folder;
            if (!QFileInfo(in.folder.local).isDir()) {
                fp.error = tr("%1 doesn't exist on this PC.").arg(in.folder.local);
                plan.folders << fp;
                continue;
            }
            fp.local = listLocal(in.folder.local);
            bool ok = false;
            const QString prefix = in.folder.remote + QLatin1Char('/');
            for (const Transport::File &r : phone->list(in.folder.remote, &ok)) {
                if (!r.path.startsWith(prefix) || !Scanner::isAudioFile(r.path))
                    continue;
                fp.remote.push_back({r.path.mid(prefix.size()), r.size});
            }
            if (!ok && fp.remote.isEmpty())
                fp.error = tr("Couldn't read %1 on the phone (it may not exist yet — it will be created).")
                               .arg(in.folder.remote);
            fp.items = SyncPlan::plan(fp.local, fp.remote, in.synced, in.removed, mode);
            plan.folders << fp;
        }
        plan.freeOnPhone = phone->freeBytes(inputs.first().folder.remote.section(QLatin1Char('/'), 0, 2));
        return plan;
    }));
}

void SyncSession::run(const QString &transport, const QString &phoneId, const Plan &plan)
{
    if (busy())
        return;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    QPointer<SyncSession> self(this);

    int total = 0;
    qint64 totalBytes = 0;
    for (const FolderPlan &fp : plan.folders)
        for (const SyncPlan::Item &i : fp.items)
            if (i.checked && i.action != SyncPlan::Action::Conflict) {
                ++total;
                if (i.action == SyncPlan::Action::Push || i.action == SyncPlan::Action::Pull)
                    totalBytes += i.size;
            }

    // Worker results that need the GUI thread (database, library) come back as a list.
    struct Outcome {
        Report report;
        QVector<QPair<QString, QSet<QString>>> states;  // local root → in-sync set
        QVector<QPair<QString, qint64>> trashedLocal;    // removed here because the phone deleted them
    };
    auto outcome = std::make_shared<Outcome>();

    m_runWatch.setFuture(QtConcurrent::run([=]() -> Report {
        Report &rep = outcome->report;
        const auto phone = Transport::open(transport);
        if (!phone || !phone->ok()) {
            rep.failed = 1;
            rep.errors << (phone ? phone->error() : tr("This connection type isn't available in this build."));
            return rep;
        }
        // MTP addresses files by id: a listing first teaches the transport where everything is.
        if (transport.startsWith(QLatin1String("mtp:")))
            for (const FolderPlan &fp : plan.folders)
                phone->list(fp.folder.remote, nullptr);
        int done = 0;
        qint64 bytes = 0;
        auto tick = [&](const QString &name) {
            QMetaObject::invokeMethod(qApp, [self, done, total, bytes, totalBytes, name] {
                if (self)
                    Q_EMIT self->progress(done, total, bytes, totalBytes, name);
            });
        };
        for (const FolderPlan &fp : plan.folders) {
            QVector<SyncPlan::Item> succeeded;
            for (const SyncPlan::Item &i : fp.items) {
                if (i.action == SyncPlan::Action::Conflict) {
                    succeeded << i;  // keeps it out of the in-sync set
                    continue;
                }
                if (!i.checked)
                    continue;
                if (cancel->load()) {
                    rep.cancelled = true;
                    break;
                }
                const QString local = fp.folder.local + QLatin1Char('/') + i.localRel;
                const QString remote = fp.folder.remote + QLatin1Char('/') + i.remoteRel;
                tick(i.localRel);
                QString err;
                bool ok = false;
                switch (i.action) {
                case SyncPlan::Action::Push: {
                    // Copy under a temporary name, then rename: a cancelled or failed copy
                    // never leaves half a song where the phone's player would find it.
                    const QString part = remote + QStringLiteral(".cbpart");
                    ok = phone->push(local, part, &err) && phone->rename(part, remote, &err);
                    if (!ok)
                        phone->remove(part, nullptr);
                    if (ok)
                        ++rep.pushed;
                    break;
                }
                case SyncPlan::Action::Pull: {
                    const QString part = local + QStringLiteral(".cbpart");
                    QDir().mkpath(QFileInfo(local).absolutePath());
                    ok = phone->pull(remote, part, &err);
                    if (ok && QFileInfo::exists(local)) {
                        err = QStringLiteral("a file with that name appeared here meanwhile");
                        ok = false;
                    }
                    ok = ok && QFile::rename(part, local);
                    if (!ok)
                        QFile::remove(part);
                    if (ok)
                        ++rep.pulled;
                    break;
                }
                case SyncPlan::Action::DeleteRemote:
                    ok = phone->remove(remote, &err);
                    if (ok)
                        ++rep.deletedRemote;
                    break;
                case SyncPlan::Action::DeleteLocal:
                    ok = QFile::moveToTrash(local);
                    if (ok) {
                        ++rep.deletedLocal;
                        outcome->trashedLocal.push_back({local, i.size});
                    } else {
                        err = QStringLiteral("couldn't move to the Trash");
                    }
                    break;
                case SyncPlan::Action::Conflict:
                    break;
                }
                if (ok) {
                    succeeded << i;
                    if (i.action == SyncPlan::Action::Push || i.action == SyncPlan::Action::Pull)
                        bytes += i.size;
                } else {
                    ++rep.failed;
                    rep.errors << QStringLiteral("%1: %2").arg(i.localRel, err.isEmpty() ? QStringLiteral("failed") : err);
                }
                ++done;
            }
            outcome->states.push_back({fp.folder.local, SyncPlan::afterSync(fp.local, fp.remote, succeeded)});
            if (rep.cancelled)
                break;
        }
        rep.bytes = bytes;
        tick(QString());
        return rep;
    }));

    // Book-keeping on this thread once the worker is done.
    m_afterRun = [this, outcome, phoneId] {
        for (const auto &[root, state] : outcome->states)
            m_library->setSyncState(phoneId, root, state);
        for (const auto &[path, size] : outcome->trashedLocal)
            m_library->addTombstone(path, size, QStringLiteral("deleted on phone"));
        const Report &r = outcome->report;
        if (r.pulled || r.deletedLocal)
            m_library->rescan();
    };
}
