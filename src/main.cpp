// SPDX-License-Identifier: GPL-3.0-or-later
// CyanBeat — local music player. Copyright (C) 2026 JohnnyGentoo Studios.
#include "engine/Player.h"
#include "library/Library.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>

#ifdef CYANBEAT_HAVE_MPRIS
#include "platform/Mpris.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#endif

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("JohnnyGentoo Studios"));
    QApplication::setApplicationName(QStringLiteral("CyanBeat"));
    QApplication::setApplicationVersion(QStringLiteral(CYANBEAT_VERSION));
    // Wayland app_id / taskbar grouping; matches the installed .desktop file.
    QApplication::setDesktopFileName(QStringLiteral("com.johnnygentoo.CyanBeat"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("CyanBeat — local music player"));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption snapshotOpt(QStringLiteral("snapshot"),
                                QStringLiteral("Save a screenshot after the library loads, then quit."),
                                QStringLiteral("png"));
    cli.addOption(snapshotOpt);
    cli.process(app);

    const bool snapshot = cli.isSet(snapshotOpt);

#ifdef CYANBEAT_HAVE_MPRIS
    // One player per desktop: a second launch just raises the first window.
    if (!snapshot) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (bus.isConnected() && bus.interface()->isServiceRegistered(Mpris::ServiceName)) {
            QDBusInterface(Mpris::ServiceName, QStringLiteral("/org/mpris/MediaPlayer2"),
                           QStringLiteral("org.mpris.MediaPlayer2"))
                .call(QStringLiteral("Raise"));
            return 0;
        }
    }
#endif

    Theme::apply(app);

    Library library;
    Player player(&library);
    MainWindow window(&library, &player);
    if (snapshot)
        window.setSnapshotPath(cli.value(snapshotOpt));

#ifdef CYANBEAT_HAVE_MPRIS
    // Headless snapshot runs stay off the bus so they never show up in waybar.
    Mpris mpris(&library, &player);
    mpris.onRaise = [&window] {
        window.show();
        window.setWindowState((window.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        window.raise();
        window.activateWindow();
    };
    mpris.onQuit = [&window] { window.close(); };
    if (!snapshot && !mpris.registerOnBus())
        qWarning("CyanBeat: could not register %s on the session bus", qPrintable(Mpris::ServiceName));
#endif

    window.show();
    library.start();
    return app.exec();
}
