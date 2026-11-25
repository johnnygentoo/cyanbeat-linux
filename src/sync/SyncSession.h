// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SyncPlan.h"

#include <QFutureWatcher>
#include <QObject>
#include <atomic>
#include <functional>
#include <memory>

class Library;

// One sync with one phone: scan both sides into a plan, then run the checked items.
class SyncSession : public QObject {
    Q_OBJECT
public:
    struct Folder {
        QString local;   // absolute, this PC
        QString remote;  // absolute, on the phone
        bool enabled = true;
    };
    static QVector<Folder> folders();  // saved list; default ~/Music ↔ /sdcard/Music
    static void setFolders(const QVector<Folder> &folders);

    struct FolderPlan {
        Folder folder;
        QVector<SyncPlan::File> local, remote;
        QVector<SyncPlan::Item> items;
        QString error;
    };
    struct Plan {
        QVector<FolderPlan> folders;
        qint64 freeOnPhone = -1;
        QString error;
    };
    struct Report {
        int pushed = 0, pulled = 0, deletedRemote = 0, deletedLocal = 0, failed = 0;
        qint64 bytes = 0;
        QStringList errors;
        bool cancelled = false;
    };

    SyncSession(Library *library, QObject *parent = nullptr);
    ~SyncSession() override;

    bool busy() const { return m_planWatch.isRunning() || m_runWatch.isRunning(); }
    void scan(const QString &transport, const QString &phoneId, SyncPlan::Mode mode);
    void run(const QString &transport, const QString &phoneId, const Plan &plan);  // checked items only
    void cancel();

Q_SIGNALS:
    void status(const QString &text);
    void planned(const SyncSession::Plan &plan);
    void progress(int done, int total, qint64 bytesDone, qint64 bytesTotal, const QString &current);
    void finished(const SyncSession::Report &report);

private:
    Library *m_library;
    QFutureWatcher<Plan> m_planWatch;
    QFutureWatcher<Report> m_runWatch;
    std::shared_ptr<std::atomic_bool> m_cancel;
    std::function<void()> m_afterRun;
};
