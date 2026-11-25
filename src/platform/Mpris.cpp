// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/Mpris.h"

#include "library/Library.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QUrl>

const QString Mpris::ServiceName = QStringLiteral("org.mpris.MediaPlayer2.cyanbeat");

namespace {
const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kRootIface = QStringLiteral("org.mpris.MediaPlayer2");
const QString kPlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QDBusObjectPath kNoTrack(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
}

Mpris::Mpris(Library *library, Player *player, QObject *parent)
    : QObject(parent), m_library(library), m_player(player)
{
    new MprisRootAdaptor(this);
    auto *playerAdaptor = new MprisPlayerAdaptor(this);

    connect(player, &Player::stateChanged, this, [this] {
        notifyPlayer({QStringLiteral("PlaybackStatus"), QStringLiteral("CanPlay"), QStringLiteral("CanPause"),
                      QStringLiteral("CanSeek")});
    });
    connect(player, &Player::currentChanged, this, [this] {
        notifyPlayer({QStringLiteral("Metadata"), QStringLiteral("CanGoNext"), QStringLiteral("CanGoPrevious"),
                      QStringLiteral("CanSeek")});
    });
    connect(library, &Library::trackUpdated, this, [this](int index) {
        if (index == m_player->currentIndex())
            notifyPlayer({QStringLiteral("Metadata")});  // rating / play count
    });
    connect(player, &Player::shuffleChanged, this, [this] { notifyPlayer({QStringLiteral("Shuffle")}); });
    connect(player, &Player::repeatChanged, this, [this] {
        notifyPlayer({QStringLiteral("LoopStatus"), QStringLiteral("CanGoNext")});
    });
    connect(player, &Player::volumeChanged, this, [this] { notifyPlayer({QStringLiteral("Volume")}); });
    connect(player, &Player::seeked, playerAdaptor, [playerAdaptor](qint64 ms) {
        Q_EMIT playerAdaptor->Seeked(ms * 1000);
    });
}

Mpris::~Mpris()
{
    if (m_registered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject(kObjectPath);
        bus.unregisterService(ServiceName);
    }
}

bool Mpris::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(ServiceName))
        return false;
    if (!bus.registerObject(kObjectPath, this, QDBusConnection::ExportAdaptors)) {
        bus.unregisterService(ServiceName);
        return false;
    }
    m_registered = true;
    return true;
}

QDBusObjectPath Mpris::currentTrackId() const
{
    const int li = m_player->currentIndex();
    if (li < 0)
        return kNoTrack;
    return QDBusObjectPath(QStringLiteral("/com/johnnygentoo/CyanBeat/track/%1").arg(li));
}

QVariantMap Mpris::metadata() const
{
    QVariantMap m;
    const Track *t = m_player->currentTrack();
    m.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(currentTrackId()));
    if (!t)
        return m;
    m.insert(QStringLiteral("mpris:length"), qlonglong(m_player->durationMs()) * 1000);
    m.insert(QStringLiteral("xesam:url"), QUrl::fromLocalFile(t->path).toString());
    m.insert(QStringLiteral("xesam:title"), t->title);
    if (!t->artist.isEmpty())
        m.insert(QStringLiteral("xesam:artist"), QStringList{t->artist});
    if (!t->album.isEmpty())
        m.insert(QStringLiteral("xesam:album"), t->album);
    if (!t->albumArtist.isEmpty())
        m.insert(QStringLiteral("xesam:albumArtist"), QStringList{t->albumArtist});
    if (!t->genre.isEmpty())
        m.insert(QStringLiteral("xesam:genre"), QStringList{t->genre});
    if (t->trackNo > 0)
        m.insert(QStringLiteral("xesam:trackNumber"), t->trackNo);
    if (t->discNo > 0)
        m.insert(QStringLiteral("xesam:discNumber"), t->discNo);
    if (t->year > 0)
        m.insert(QStringLiteral("xesam:contentCreated"), QStringLiteral("%1-01-01T00:00:00").arg(t->year));
    m.insert(QStringLiteral("xesam:useCount"), t->playCount);
    m.insert(QStringLiteral("xesam:userRating"), t->rating / 5.0);
    return m;
}

void Mpris::notify(const QString &interface, const QVariantMap &changed)
{
    if (!m_registered || changed.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createSignal(kObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    msg << interface << changed << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

void Mpris::notifyPlayer(const QStringList &props)
{
    // Read through the adaptor so the values match what Get() would return.
    auto *adaptor = findChild<MprisPlayerAdaptor *>();
    if (!adaptor)
        return;
    QVariantMap changed;
    for (const QString &p : props)
        changed.insert(p, adaptor->property(p.toLatin1().constData()));
    notify(kPlayerIface, changed);
}

// ---------------------------------------------------------------- root

MprisRootAdaptor::MprisRootAdaptor(Mpris *parent) : QDBusAbstractAdaptor(parent), m_mpris(parent)
{
    setAutoRelaySignals(true);
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
            QStringLiteral("audio/opus"), QStringLiteral("audio/mp4"), QStringLiteral("audio/x-wav"),
            QStringLiteral("audio/x-ms-wma"), QStringLiteral("audio/webm")};
}

void MprisRootAdaptor::Raise()
{
    if (m_mpris->onRaise)
        m_mpris->onRaise();
}

void MprisRootAdaptor::Quit()
{
    if (m_mpris->onQuit)
        m_mpris->onQuit();
}

// ---------------------------------------------------------------- player

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris *parent)
    : QDBusAbstractAdaptor(parent), m_mpris(parent), m_player(parent->player())
{
    setAutoRelaySignals(true);
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    switch (m_player->state()) {
    case GstEngine::State::Playing: return QStringLiteral("Playing");
    case GstEngine::State::Paused: return QStringLiteral("Paused");
    default: return QStringLiteral("Stopped");
    }
}

QString MprisPlayerAdaptor::loopStatus() const
{
    switch (m_player->repeat()) {
    case Player::Repeat::One: return QStringLiteral("Track");
    case Player::Repeat::All: return QStringLiteral("Playlist");
    default: return QStringLiteral("None");
    }
}

void MprisPlayerAdaptor::setLoopStatus(const QString &s)
{
    if (s == QLatin1String("Track"))
        m_player->setRepeat(Player::Repeat::One);
    else if (s == QLatin1String("Playlist"))
        m_player->setRepeat(Player::Repeat::All);
    else
        m_player->setRepeat(Player::Repeat::Off);
}

bool MprisPlayerAdaptor::shuffle() const { return m_player->shuffle(); }
void MprisPlayerAdaptor::setShuffle(bool on) { m_player->setShuffle(on); }
QVariantMap MprisPlayerAdaptor::metadata() const { return m_mpris->metadata(); }
double MprisPlayerAdaptor::volume() const { return m_player->volume(); }
void MprisPlayerAdaptor::setVolume(double v) { m_player->setVolume(v); }
qlonglong MprisPlayerAdaptor::position() const { return qlonglong(m_player->positionMs()) * 1000; }
bool MprisPlayerAdaptor::canGoNext() const { return m_player->hasNext(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_player->hasPrevious(); }
bool MprisPlayerAdaptor::canPlay() const { return true; }
bool MprisPlayerAdaptor::canSeek() const { return m_player->currentIndex() >= 0; }

void MprisPlayerAdaptor::Next() { m_player->next(); }
void MprisPlayerAdaptor::Previous() { m_player->previous(); }
void MprisPlayerAdaptor::Pause() { m_player->pause(); }
void MprisPlayerAdaptor::PlayPause() { m_player->togglePlay(); }
void MprisPlayerAdaptor::Stop() { m_player->stop(); }
void MprisPlayerAdaptor::Play() { m_player->play(); }

void MprisPlayerAdaptor::Seek(qlonglong offsetUs)
{
    if (m_player->currentIndex() < 0)
        return;
    const qint64 target = m_player->positionMs() + offsetUs / 1000;
    if (target >= m_player->durationMs() && m_player->durationMs() > 0)
        m_player->next();  // per spec: seeking past the end acts like Next
    else
        m_player->seek(qMax<qint64>(0, target));
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
{
    if (trackId != m_mpris->currentTrackId() || positionUs < 0)
        return;
    if (m_player->durationMs() > 0 && positionUs / 1000 > m_player->durationMs())
        return;
    m_player->seek(positionUs / 1000);
}

void MprisPlayerAdaptor::OpenUri(const QString &uri)
{
    const QUrl url(uri);
    if (!url.isLocalFile())
        return;
    const int li = m_mpris->library()->indexOfPath(url.toLocalFile());
    if (li >= 0)
        m_player->playQueue({li}, 0);
}
