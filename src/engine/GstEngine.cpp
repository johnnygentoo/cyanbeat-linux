// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/GstEngine.h"

#include <QPointer>
#include <QUrl>
#include <gst/audio/audio.h>
#include <gst/audio/streamvolume.h>
#include <gst/gst.h>
#include <cmath>

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

    if (GstElementFactory *f = gst_element_factory_find("pitch")) {
        m_pitchAvailable = true;
        gst_object_unref(f);
    }
    buildFilters(false);
    g_signal_connect(m_pipeline, "about-to-finish", G_CALLBACK(+[](GstElement *pipeline, gpointer self) {
        static_cast<GstEngine *>(self)->onAboutToFinish(pipeline);
    }), this);

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, reinterpret_cast<GstBusSyncHandler>(&GstEngine::busSyncHandler),
                             this, nullptr);
    gst_object_unref(bus);
    setVolume(m_volume);
}

void GstEngine::buildFilters(bool withPitch)
{
    // volume(ReplayGain) ! equalizer-nbands ! speed ! scaletempo ! freeverb !
    // equalizer-nbands(bass) ! rglimiter, with audioconverts between, and a buffer
    // probe on the limiter's output: the audio exactly as heard, without a second
    // sink in the pipeline (a synced appsink stalls inside playbin's filter; an
    // unsynced one runs ahead and skews position queries).
    auto make = [](const char *factory) { return gst_element_factory_make(factory, nullptr); };
    GstElement *rg = make("volume"), *eq = make("equalizer-nbands"), *lim = make("rglimiter"),
               *bass = make("equalizer-nbands");
    // Effects are optional: without them you just don't get the vaporwave knobs.
    // Pitch is SoundTouch's `pitch` (gst-plugins-soundtouch): it shifts pitch without
    // touching timestamps. (The old `speed` element rewrites them, which broke
    // gapless — the next song looked late and was dropped — and the position readout.)
    // SoundTouch also mangles the timeline across a gapless switch (song 3 started
    // late, position ran past the end, EOS never came), so it's only in the chain
    // while pitch ≠ 1 — setEffects() rebuilds the chain when that flips.
    GstElement *pitch = withPitch ? make("pitch") : nullptr;
    GstElement *tempo = make("scaletempo"), *verb = make("freeverb");
    if (!rg || !eq || !lim || !bass) {
        for (GstElement *x : {rg, eq, lim, bass, pitch, tempo, verb})
            if (x)
                gst_object_unref(gst_object_ref_sink(x));
        qWarning("CyanBeat: audio filter elements missing; EQ and visualizer disabled");
        return;
    }
    QVector<GstElement *> chain;
    auto conv = [&] { chain << make("audioconvert"); };
    conv();
    chain << rg;
    conv();
    chain << eq;
    conv();
    // Each effect is optional on its own.
    for (GstElement *fxe : {pitch, tempo, verb}) {
        if (!fxe)
            continue;
        chain << fxe;
        conv();
    }
    if (!tempo || !verb)
        qWarning("CyanBeat: scaletempo/freeverb missing; speed and reverb unavailable");
    chain << bass;
    conv();
    chain << lim;
    conv();

    GstElement *bin = gst_bin_new("cyanbeat-filters");
    for (GstElement *e : std::as_const(chain))
        gst_bin_add(GST_BIN(bin), e);
    g_object_set(eq, "num-bands", 8, nullptr);
    g_object_set(bass, "num-bands", 1, nullptr);
    if (GObject *b = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(bass), 0)) {
        g_object_set(b, "freq", 70.0, "bandwidth", 90.0, "gain", 0.0, nullptr);
        g_object_unref(b);
    }
    g_object_set(lim, "enabled", FALSE, nullptr);
    if (verb)
        g_object_set(verb, "level", 0.0f, "room-size", 0.5f, "damping", 0.4f, nullptr);
    bool linked = true;
    for (int i = 0; i + 1 < chain.size(); ++i)
        linked = linked && gst_element_link(chain[i], chain[i + 1]);
    if (!linked) {
        gst_object_unref(gst_object_ref_sink(bin));
        qWarning("CyanBeat: could not link audio filters; EQ and visualizer disabled");
        return;
    }
    GstPad *in = gst_element_get_static_pad(chain.first(), "sink");
    GstPad *out = gst_element_get_static_pad(chain.last(), "src");
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", in));
    gst_element_add_pad(bin, gst_ghost_pad_new("src", out));
    gst_object_unref(in);
    gst_object_unref(out);

    // rglimiter only speaks F32 — the probe always sees interleaved float.
    GstPad *tapPad = gst_element_get_static_pad(lim, "src");
    gst_pad_add_probe(tapPad,
                      GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_FLUSH
                                      | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                      reinterpret_cast<GstPadProbeCallback>(&GstEngine::onTapProbe), this, nullptr);
    gst_object_unref(tapPad);

    g_object_set(m_pipeline, "audio-filter", bin, nullptr);
    m_eq = eq;
    m_limiter = lim;
    m_rgVolume = rg;
    m_bass = bass;
    m_pitch = pitch;
    m_filterHasPitch = pitch != nullptr;
    m_tempo = tempo;
    m_reverb = verb;
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
    if (GST_PAD_PROBE_INFO_TYPE(info) & (GST_PAD_PROBE_TYPE_EVENT_FLUSH | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)) {
        const GstEventType t = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info));
        // Seek, or the next song starting (gapless): its times restart, drop what's queued.
        if (t == GST_EVENT_FLUSH_STOP || t == GST_EVENT_STREAM_START)
            (*tap)(nullptr, 0, 0, 0, -1);
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
    m_eqFreqs = freqsHz;
    m_eqBandwidths = bandwidthsHz;
    m_eqGains = gainsDb;
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
    m_limitEq = on;
    updateLimiter();
}

void GstEngine::updateLimiter()
{
    // Anything that can push the level up gets the soft limiter; plain playback doesn't.
    if (m_limiter)
        g_object_set(m_limiter, "enabled", gboolean(m_limitEq || m_rgDb > 0.0 || m_fx.bassDb > 0.0 || m_fx.reverb > 0.0),
                     nullptr);
}

void GstEngine::setReplayGainDb(double db)
{
    m_rgDb = qBound(-30.0, db, 15.0);
    if (m_rgVolume)
        g_object_set(m_rgVolume, "volume", std::pow(10.0, m_rgDb / 20.0), nullptr);
    updateLimiter();
}

void GstEngine::setEffects(const Effects &fx)
{
    m_fx = fx;
    m_fx.speed = qBound(0.5, fx.speed, 2.0);
    m_fx.pitch = qBound(0.5, fx.pitch, 2.0);
    m_fx.reverb = qBound(0.0, fx.reverb, 1.0);
    if (!m_pitchAvailable)
        m_fx.pitch = 1.0;
    if (!m_tempo)
        m_fx.speed = 1.0;
    const bool needPitch = m_pitchAvailable && std::abs(m_fx.pitch - 1.0) > 1e-3;
    if (needPitch != m_filterHasPitch)
        rebuildFilters(needPitch);  // also applies m_fx
    else
        applyEffects(true);
}

void GstEngine::applyEffects(bool reseekForRate)
{
    // Tempo = the playback rate, time-stretched by scaletempo (pitch kept); pitch =
    // SoundTouch on top. Classic vaporwave (0.8 / 0.8) = rate 0.8 + pitch 0.8.
    if (m_pitch)
        g_object_set(m_pitch, "pitch", float(m_fx.pitch), nullptr);
    if (m_reverb)
        g_object_set(m_reverb, "level", float(m_fx.reverb * 0.55), "room-size", float(0.35 + 0.6 * m_fx.reverb),
                     nullptr);
    if (m_bass)
        if (GObject *b = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(m_bass), 0)) {
            g_object_set(b, "gain", qBound(0.0, m_fx.bassDb, 12.0), nullptr);
            g_object_unref(b);
        }
    updateLimiter();
    const double rate = m_fx.speed;
    if (!qFuzzyCompare(rate, m_rate)) {
        m_rate = rate;
        // (GST_SEEK_FLAG_INSTANT_RATE_CHANGE reported success but applied late here.)
        if (reseekForRate && m_state != State::Stopped)
            seek(positionMs());
    }
}

void GstEngine::rebuildFilters(bool withPitch)
{
    // playbin only takes a new audio-filter while it isn't running: stop, swap,
    // restore every setting, start again and seek back to where we were.
    const State was = m_state;
    const qint64 pos = positionMs();
    gst_element_set_state(m_pipeline, GST_STATE_READY);
    m_gaplessPending = false;
    buildFilters(withPitch);
    if (!m_eqFreqs.isEmpty())
        setEqualizer(m_eqFreqs, m_eqBandwidths, m_eqGains);
    setReplayGainDb(m_rgDb);
    applyEffects(false);
    if (was != State::Stopped) {
        m_pendingSeekMs = pos;
        m_state = State::Paused;  // so the PLAYING report is passed on
        gst_element_set_state(m_pipeline, was == State::Playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    }
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

void GstEngine::setNextFile(const QString &path)
{
    std::lock_guard lock(m_nextMutex);
    m_nextUri = path.isEmpty() ? QByteArray() : QUrl::fromLocalFile(path).toEncoded();
}

void GstEngine::onAboutToFinish(GstElement *pipeline)
{
    // Streaming thread, near the end of the current song: queue the next one in
    // the same pipeline so it starts without a gap.
    QByteArray uri;
    {
        std::lock_guard lock(m_nextMutex);
        uri = m_nextUri;
        m_nextUri.clear();
    }
    if (uri.isEmpty() || m_filterHasPitch)
        return;  // end of the queue (or pitch shifting on: plain EOS → next song)
    m_gaplessPending = true;
    g_object_set(pipeline, "uri", uri.constData(), nullptr);
}

int GstEngine::busSyncHandler(GstBus *, GstMessage *msg, void *self)
{
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_STATE_CHANGED:
    case GST_MESSAGE_DURATION_CHANGED:
    case GST_MESSAGE_STREAM_START:
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
    case GST_MESSAGE_STREAM_START:
        if (m_gaplessPending.exchange(false)) {
            // The next song's segment arrives at rate 1; with a speed effect on, put the
            // tempo back (a brief hiccup at the switch — only when speed ≠ 1).
            if (!qFuzzyCompare(m_rate, 1.0))
                seek(0);
            Q_EMIT gaplessAdvanced();
        }
        break;
    case GST_MESSAGE_DURATION_CHANGED:
    case GST_MESSAGE_ASYNC_DONE:
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ASYNC_DONE) {
            if (m_pendingSeekMs >= 0) {  // back to where we were after a chain rebuild
                const qint64 to = m_pendingSeekMs;
                m_pendingSeekMs = -1;
                m_rateStale = false;
                seek(to);
            } else if (!qFuzzyCompare(m_rate, 1.0) && m_rateStale) {
                m_rateStale = false;
                seek(positionMs());  // a freshly loaded song plays at rate 1 until told otherwise
            }
        }
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
    m_gaplessPending = false;
    m_rateStale = true;
    m_pendingSeekMs = -1;
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
    gst_element_seek(m_pipeline, m_rate, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                     GST_SEEK_TYPE_SET, qMax<qint64>(0, ms) * GST_MSECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
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
