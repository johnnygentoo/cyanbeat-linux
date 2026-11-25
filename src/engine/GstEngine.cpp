// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/GstEngine.h"

#include <QPointer>
#include <QUrl>
#include <gst/audio/audio.h>
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

    buildFilters();

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, reinterpret_cast<GstBusSyncHandler>(&GstEngine::busSyncHandler),
                             this, nullptr);
    gst_object_unref(bus);
    setVolume(m_volume);
}

void GstEngine::buildFilters()
{
    // audioconvert ! equalizer-nbands ! audioconvert ! rglimiter ! audioconvert
    // with a buffer probe on the limiter's output: the audio exactly as heard,
    // without a second sink in the pipeline (a synced appsink stalls inside
    // playbin's filter; an unsynced one runs ahead and skews position queries).
    auto make = [](const char *factory) { return gst_element_factory_make(factory, nullptr); };
    GstElement *conv1 = make("audioconvert"), *eq = make("equalizer-nbands"), *conv2 = make("audioconvert"),
               *lim = make("rglimiter"), *conv3 = make("audioconvert");
    GstElement *all[] = {conv1, eq, conv2, lim, conv3};
    for (GstElement *e : all) {
        if (!e) {
            // Missing plugin: play without EQ/visualizer rather than not at all.
            for (GstElement *x : all)
                if (x)
                    gst_object_unref(gst_object_ref_sink(x));
            qWarning("CyanBeat: audio filter elements missing; EQ and visualizer disabled");
            return;
        }
    }
    GstElement *bin = gst_bin_new("cyanbeat-filters");
    for (GstElement *e : all)
        gst_bin_add(GST_BIN(bin), e);
    g_object_set(eq, "num-bands", 8, nullptr);
    g_object_set(lim, "enabled", FALSE, nullptr);
    if (!gst_element_link_many(conv1, eq, conv2, lim, conv3, nullptr)) {
        gst_object_unref(gst_object_ref_sink(bin));
        qWarning("CyanBeat: could not link audio filters; EQ and visualizer disabled");
        return;
    }
    GstPad *in = gst_element_get_static_pad(conv1, "sink");
    GstPad *out = gst_element_get_static_pad(conv3, "src");
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", in));
    gst_element_add_pad(bin, gst_ghost_pad_new("src", out));
    gst_object_unref(in);
    gst_object_unref(out);

    // rglimiter only speaks F32 — the probe always sees interleaved float.
    GstPad *tapPad = gst_element_get_static_pad(lim, "src");
    gst_pad_add_probe(tapPad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_FLUSH),
                      reinterpret_cast<GstPadProbeCallback>(&GstEngine::onTapProbe), this, nullptr);
    gst_object_unref(tapPad);

    g_object_set(m_pipeline, "audio-filter", bin, nullptr);
    m_eq = eq;
    m_limiter = lim;
}

int GstEngine::onTapProbe(GstPad *pad, GstPadProbeInfo *info, void *self)
{
    auto *engine = static_cast<GstEngine *>(self);
    std::shared_ptr<PcmTap> tap;
    {
        std::lock_guard lock(engine->m_tapMutex);
        tap = engine->m_tap;
    }
    if (!tap || !*tap)
        return GST_PAD_PROBE_OK;
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_FLUSH) {
        if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_FLUSH_STOP)
            (*tap)(nullptr, 0, 0, 0, -1);  // seek: drop anything queued
        return GST_PAD_PROBE_OK;
    }
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf || !GST_BUFFER_PTS_IS_VALID(buf))
        return GST_PAD_PROBE_OK;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps)
        return GST_PAD_PROBE_OK;
    GstAudioInfo ai;
    const bool ok = gst_audio_info_from_caps(&ai, caps) && GST_AUDIO_INFO_FORMAT(&ai) == GST_AUDIO_FORMAT_F32LE
                    && GST_AUDIO_INFO_LAYOUT(&ai) == GST_AUDIO_LAYOUT_INTERLEAVED;
    gst_caps_unref(caps);
    if (!ok)
        return GST_PAD_PROBE_OK;
    // Stream time = what position queries report, so the consumer can line chunks up with playback.
    qint64 ptsMs = -1;
    if (GstEvent *segEv = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0)) {
        const GstSegment *s = nullptr;
        gst_event_parse_segment(segEv, &s);
        if (s) {
            const guint64 st = gst_segment_to_stream_time(s, GST_FORMAT_TIME, GST_BUFFER_PTS(buf));
            if (st != GST_CLOCK_TIME_NONE)
                ptsMs = qint64(st / GST_MSECOND);
        }
        gst_event_unref(segEv);
    }
    if (ptsMs < 0)
        return GST_PAD_PROBE_OK;
    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
        const int channels = GST_AUDIO_INFO_CHANNELS(&ai);
        const int frames = int(map.size / (sizeof(float) * qMax(1, channels)));
        (*tap)(reinterpret_cast<const float *>(map.data), frames, GST_AUDIO_INFO_RATE(&ai), channels, ptsMs);
        gst_buffer_unmap(buf, &map);
    }
    return GST_PAD_PROBE_OK;
}

void GstEngine::setPcmTap(PcmTap tap)
{
    std::lock_guard lock(m_tapMutex);
    m_tap = tap ? std::make_shared<PcmTap>(std::move(tap)) : nullptr;
}

void GstEngine::setEqualizer(const QVector<double> &freqsHz, const QVector<double> &bandwidthsHz,
                             const QVector<double> &gainsDb)
{
    if (!m_eq)
        return;
    const int n = int(qMin(freqsHz.size(), qMin(bandwidthsHz.size(), gainsDb.size())));
    g_object_set(m_eq, "num-bands", n, nullptr);
    for (int i = 0; i < n; ++i) {
        GObject *band = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(m_eq), guint(i));
        if (!band)
            continue;
        g_object_set(band, "freq", freqsHz[i], "bandwidth", bandwidthsHz[i],
                     "gain", qBound(-24.0, gainsDb[i], 12.0), "type", 0 /* peak */, nullptr);
        g_object_unref(band);
    }
}

void GstEngine::setLimiter(bool on)
{
    if (m_limiter)
        g_object_set(m_limiter, "enabled", gboolean(on), nullptr);
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
