// SPDX-License-Identifier: GPL-3.0-or-later
// Regression: clicking a phone under DEVICES made it the sidebar's current item,
// which reached the track list as a bogus source and blanked it until restart.
#include "ui/SourceList.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QTreeWidgetItemIterator>
#include <cstdio>

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
                           else std::fprintf(stderr, "ok: %s\n", msg); } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    SourceList list;
    list.setPlaylists({{7, QStringLiteral("Night Drive")}});
    list.setPhones({{QStringLiteral("R5CY"), QStringLiteral("SM-S936U · USB"), true}});

    QVector<Source> seen;
    QStringList opened;
    QObject::connect(&list, &SourceList::sourceChanged, [&](const Source &s) { seen << s; });
    QObject::connect(&list, &SourceList::phoneActivated, [&](const QString &id) { opened << id; });

    QTreeWidgetItem *phone = nullptr, *playlist = nullptr;
    for (QTreeWidgetItemIterator it(&list); *it; ++it) {
        if ((*it)->text(0).startsWith(QLatin1String("SM-S936U")))
            phone = *it;
        if ((*it)->text(0) == QLatin1String("Night Drive"))
            playlist = *it;
    }
    CHECK(phone && playlist, "phone and playlist are in the sidebar");

    list.setCurrentItem(playlist);
    CHECK(!seen.isEmpty() && seen.last().kind == Source::Playlist && seen.last().id == 7, "picking a playlist shows it");
    seen.clear();
    list.setCurrentItem(phone);  // what a click on the phone does
    bool bogus = false;
    for (const Source &s : seen)
        bogus = bogus || (s.kind != Source::Library && s.kind != Source::Smart && s.kind != Source::Playlist);
    CHECK(!bogus, "a phone never reaches the track list as a source");
    CHECK(list.current().kind == Source::Playlist && list.current().id == 7, "selection snaps back to the playlist");
    Q_EMIT list.itemClicked(phone, 0);
    CHECK(opened == QStringList{QStringLiteral("R5CY")}, "clicking the phone still opens Sync for it");

    // Phone list refreshes (every 10 s) must not disturb the current view either.
    list.setPhones({});
    list.setPhones({{QStringLiteral("R5CY"), QStringLiteral("SM-S936U · Wi-Fi"), true}});
    CHECK(list.current().kind == Source::Playlist, "phone refresh keeps the current view");

    // Regression: reskinning re-sets item icons, which used to fire a bogus playlist
    // "rename", rebuild the list under the loop, and crash.
    int renames = 0;
    QObject::connect(&list, &SourceList::renameRequested, [&] { ++renames; });
    Theme::setVapor(true);
    Theme::setVapor(false);
    CHECK(renames == 0, "reskin doesn't rename (or crash) playlists");
    CHECK(list.current().kind == Source::Playlist, "reskin keeps the current view");

    std::fprintf(stderr, failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
