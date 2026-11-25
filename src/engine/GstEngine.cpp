// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/GstEngine.h"

#include <QPointer>
#include <QUrl>
#include <gst/audio/streamvolume.h>
#include <gst/gst.h>

namespace {
// playbin3 GstPlayFlags: audio + soft volume. No video, no subtitles, so a
// webm/mp4 from yt-dlp never pops a video window.
constexpr guint kPlayFlags = (1u << 1) | (1u << 4);
}

GstEngine::GstEngine(QObject *parent) : QObject(parent)
{
    GError *err = nullptr;
    if (!gst_init_check(nullptr, nullptr, &err)) {
        m_initError = err ? QString::fromUtf8(err->message) : QStringLiteral("gst_init failed");
        g_clear_error(&err);
        return;
    }
    m_pipeline = gst_element_factory_make("playbin3", "cyanbeat");
    if (!m_pipeline)
        m_pipeline = gst_element_factory_make("playbin", "cyanbeat");
    if (!m_pipeline) {
        m_initError = QStringLiteral("GStreamer playbin is missing (install gst-plugins-base).");
        return;
    }
    g_object_set(m_pipeline, "flags", kPlayFlags, nullptr);

    // Testing hook: CYANBEAT_AUDIOSINK=fakesink plays silently.
    const QByteArray sinkName = qgetenv("CYANBEAT_AUDIOSINK");
    if (!sinkName.isEmpty()) {
        if (GstElement *sink = gst_element_factory_make(sinkName.constData(), nullptr)) {
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "sync"))
                g_object_set(sink, "sync", TRUE, nullptr);
            g_object_set(m_pipeline, "audio-sink", sink, nullptr);
        }
    }

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, reinterpret_cast<GstBusSyncHandler>(&GstEngine::busSyncHandler),
                             this, nullptr);
    gst_object_unref(bus);
    setVolume(m_volume);
}

GstEngine::~GstEngine()
{
    if (!m_pipeline)
        return;
    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    gst_object_unref(bus);
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_object_unref(m_pipeline);
}

int GstEngine::busSyncHandler(GstBus *, GstMessage *msg, void *self)
{
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_STATE_CHANGED:
    case GST_MESSAGE_DURATION_CHANGED:
    case GST_MESSAGE_ASYNC_DONE: {
        auto *engine = static_cast<GstEngine *>(self);
        gst_message_ref(msg);
        QPointer<GstEngine> guard(engine);
        QMetaObject::invokeMethod(engine, [guard, msg] {
            if (guard)
                guard->handleMessage(msg);
            gst_message_unref(msg);
        }, Qt::QueuedConnection);
        break;
    }
    default:
        break;
    }
    return GST_BUS_DROP;
}

void GstEngine::handleMessage(GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        Q_EMIT endOfStream();
        break;
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        const QString text = err ? QString::fromUtf8(err->message) : QStringLiteral("playback error");
        g_clear_error(&err);
        g_free(debug);
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        setState(State::Stopped);
        Q_EMIT error(text);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
            GstState oldS, newS, pending;
            gst_message_parse_state_changed(msg, &oldS, &newS, &pending);
            if (newS == GST_STATE_PLAYING)
                setState(State::Playing);
            else if (newS == GST_STATE_PAUSED && pending == GST_STATE_VOID_PENDING && m_state != State::Stopped)
                setState(State::Paused);
        }
        break;
    case GST_MESSAGE_DURATION_CHANGED:
    case GST_MESSAGE_ASYNC_DONE:
        Q_EMIT durationChanged(durationMs());
        break;
    default:
        break;
    }
}

void GstEngine::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    Q_EMIT stateChanged(s);
}

void GstEngine::playFile(const QString &path)
{
    if (!m_pipeline)
        return;
    gst_element_set_state(m_pipeline, GST_STATE_READY);
    const QByteArray uri = QUrl::fromLocalFile(path).toEncoded();
    g_object_set(m_pipeline, "uri", uri.constData(), nullptr);
    // Ensures a following PAUSED report isn't swallowed as "still stopped".
    m_state = State::Paused;
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
}

void GstEngine::play()
{
    if (m_pipeline && m_state != State::Stopped)
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
}

void GstEngine::pause()
{
    if (m_pipeline && m_state == State::Playing)
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
}

void GstEngine::togglePause()
{
    if (m_state == State::Playing)
        pause();
    else
        play();
}

void GstEngine::stop()
{
    if (!m_pipeline)
        return;
    gst_element_set_state(m_pipeline, GST_STATE_READY);
    setState(State::Stopped);
}

void GstEngine::seek(qint64 ms)
{
    if (!m_pipeline || m_state == State::Stopped)
        return;
    gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
                            GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                            qMax<qint64>(0, ms) * GST_MSECOND);
}

qint64 GstEngine::positionMs() const
{
    gint64 pos = 0;
    if (m_pipeline && m_state != State::Stopped
        && gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos))
        return pos / GST_MSECOND;
    return 0;
}

qint64 GstEngine::durationMs() const
{
    gint64 dur = 0;
    if (m_pipeline && gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &dur) && dur > 0)
        return dur / GST_MSECOND;
    return 0;
}

void GstEngine::setVolume(double v)
{
    m_volume = qBound(0.0, v, 1.0);
    if (m_pipeline)
        gst_stream_volume_set_volume(GST_STREAM_VOLUME(m_pipeline), GST_STREAM_VOLUME_FORMAT_CUBIC, m_volume);
}
