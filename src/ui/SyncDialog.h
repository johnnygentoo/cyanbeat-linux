// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SyncSession.h"

#include <QDialog>

class Devices;
class Library;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// Tools › Sync Phone: pick the phone, the folders, merge or mirror; Check shows
// exactly what will happen (every copy and delete, checkable); Sync does it.
class SyncDialog : public QDialog {
    Q_OBJECT
public:
    SyncDialog(Library *library, Devices *devices, QWidget *parent = nullptr);
    void selectPhone(const QString &id);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void refreshPhones();
    void refreshFolders();
    void saveFolders();
    void updateButtons();
    void check();
    void showPlan(const SyncSession::Plan &plan);
    void collectChecks();
    void updatePlanSummary();
    void startSync();
    void onFinished(const SyncSession::Report &r);
    QString currentPhoneId() const;
    QString currentTransport() const;

    Library *m_library;
    Devices *m_devices;
    SyncSession *m_session;

    QComboBox *m_phone;
    QLabel *m_phoneState;
    QPushButton *m_wifi, *m_setupWifi, *m_refresh;
    QTableWidget *m_folders;
    QRadioButton *m_merge, *m_mirror;
    QTreeWidget *m_tree;
    QLabel *m_summary;
    QProgressBar *m_progress;
    QPushButton *m_check, *m_sync, *m_cancel;

    SyncSession::Plan m_plan;
    QString m_planPhone;  // plan belongs to this phone
    bool m_filling = false;
};
