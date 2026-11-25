// SPDX-License-Identifier: GPL-3.0-or-later
// CyanBeat — local music player. Copyright (C) 2026 JohnnyGentoo Studios.
#include "library/Library.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>

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
    QCommandLineOption snapshot(QStringLiteral("snapshot"),
                                QStringLiteral("Save a screenshot after the library loads, then quit."),
                                QStringLiteral("png"));
    cli.addOption(snapshot);
    cli.process(app);

    Theme::apply(app);

    Library library;
    MainWindow window(&library);
    if (cli.isSet(snapshot))
        window.setSnapshotPath(cli.value(snapshot));
    window.show();
    library.start();
    return app.exec();
}
