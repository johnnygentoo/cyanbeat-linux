// SPDX-License-Identifier: GPL-3.0-or-later
// The sync rules: merge keeps olds and combines news; mirror carries deletions;
// files removed here on purpose are never pulled back; nothing is deleted on a first sync.
#include "sync/SyncPlan.h"

#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
                           else std::fprintf(stderr, "ok: %s\n", msg); } while (0)

using namespace SyncPlan;

static const Item *find(const QVector<Item> &items, const QString &rel)
{
    for (const Item &i : items)
        if (i.localRel == rel || i.remoteRel == rel)
            return &i;
    return nullptr;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QVector<File> pc{{"both.mp3", 10}, {"pc-new.mp3", 20}, {"gone-from-phone.mp3", 30}, {"clash.mp3", 40},
                           {"What? Why: Me|You.mp3", 50}};
    const QVector<File> phone{{"both.mp3", 10}, {"phone-new.mp3", 60}, {"gone-from-pc.mp3", 70}, {"clash.mp3", 41},
                              {"dupe [abc].mp3", 80}, {"CyanBeat/sub.mp3", 90}};
    const QSet<QString> synced{"both.mp3", "gone-from-phone.mp3", "gone-from-pc.mp3"};
    const QSet<QString> removed{"dupe [abc].mp3"};

    CHECK(phoneName("What? Why: Me|You.mp3") == QString::fromUtf8("What？ Why： Me｜You.mp3"), "illegal characters map to full-width");

    const auto merge = plan(pc, phone, synced, removed, Mode::Merge);
    CHECK(!find(merge, "both.mp3"), "identical files: nothing to do");
    CHECK(find(merge, "pc-new.mp3") && find(merge, "pc-new.mp3")->action == Action::Push, "new on PC → phone");
    CHECK(find(merge, "phone-new.mp3") && find(merge, "phone-new.mp3")->action == Action::Pull, "new on phone → PC");
    CHECK(find(merge, "CyanBeat/sub.mp3") && find(merge, "CyanBeat/sub.mp3")->action == Action::Pull, "subfolders come along");
    CHECK(find(merge, "gone-from-phone.mp3")->action == Action::Push, "merge: deleted on phone is put back (keep olds)");
    CHECK(find(merge, "gone-from-pc.mp3")->action == Action::Pull, "merge: deleted on PC is brought back (keep olds)");
    CHECK(find(merge, "clash.mp3")->action == Action::Conflict && !find(merge, "clash.mp3")->checked,
          "same name, different size: conflict, untouched by default");
    CHECK(find(merge, "dupe [abc].mp3")->action == Action::DeleteRemote, "removed here on purpose → offered for deletion on phone");
    const Item *odd = find(merge, "What? Why: Me|You.mp3");
    CHECK(odd && odd->action == Action::Push && odd->remoteRel == QString::fromUtf8("What？ Why： Me｜You.mp3"),
          "a name the phone can't store is pushed under its full-width form");
    int deletes = 0;
    for (const Item &i : merge)
        deletes += i.action == Action::DeleteLocal || (i.action == Action::DeleteRemote && i.localRel != "dupe [abc].mp3");
    CHECK(deletes == 0, "merge never deletes anything else");

    const auto mirror = plan(pc, phone, synced, removed, Mode::Mirror);
    CHECK(find(mirror, "gone-from-phone.mp3")->action == Action::DeleteLocal, "mirror: deleted on phone → delete here");
    CHECK(find(mirror, "gone-from-pc.mp3")->action == Action::DeleteRemote, "mirror: deleted on PC → delete on phone");
    CHECK(find(mirror, "pc-new.mp3")->action == Action::Push && find(mirror, "phone-new.mp3")->action == Action::Pull,
          "mirror still copies new files both ways");

    const auto first = plan(pc, phone, {}, {}, Mode::Mirror);
    bool anyDelete = false;
    for (const Item &i : first)
        anyDelete = anyDelete || i.action == Action::DeleteLocal || i.action == Action::DeleteRemote;
    CHECK(!anyDelete, "first sync (no history) never deletes, even in mirror mode");

    // The phone already has the full-width name: that's the same file, not a new one.
    const auto mapped = plan({{"A: B.mp3", 5}}, {{QString::fromUtf8("A： B.mp3"), 5}}, {}, {}, Mode::Merge);
    CHECK(mapped.isEmpty(), "local \"A: B\" matches phone \"A： B\"");

    const auto after = afterSync(pc, phone, merge);
    CHECK(after.contains("pc-new.mp3") && after.contains("phone-new.mp3") && !after.contains("dupe [abc].mp3")
              && !after.contains("clash.mp3"),
          "state after sync: copied files in, deleted and conflicting ones out");

    std::fprintf(stderr, failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
