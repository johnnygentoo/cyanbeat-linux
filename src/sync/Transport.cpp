// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Transport.h"

#include "sync/Adb.h"
#include "sync/Mtp.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>

#ifdef CYANBEAT_HAVE_MTP
#include <libmtp.h>
#endif

namespace {

class AdbTransport : public Transport {
public:
    explicit AdbTransport(QString serial) : m_serial(std::move(serial)) {}
    bool ok() const override { return !m_serial.isEmpty(); }
    QString error() const override { return {}; }
    QVector<File> list(const QString &root, bool *ok) override
    {
        QVector<File> out;
        for (const Adb::RemoteFile &f : Adb::listFiles(m_serial, root, ok))
            out.push_back({f.path, f.size});
        return out;
    }
    qint64 freeBytes(const QString &path) override { return Adb::freeBytes(m_serial, path); }
    bool push(const QString &l, const QString &r, QString *e) override { return Adb::push(m_serial, l, r, e); }
    bool pull(const QString &r, const QString &l, QString *e) override { return Adb::pull(m_serial, r, l, e); }
    bool rename(const QString &f, const QString &t, QString *e) override { return Adb::move(m_serial, f, t, e); }
    bool remove(const QString &r, QString *e) override { return Adb::remove(m_serial, r, e); }

private:
    QString m_serial;
};

#ifdef CYANBEAT_HAVE_MTP

// "/sdcard/Music/x" → {"Music", "x"} under the primary storage.
QStringList storageParts(const QString &path)
{
    QString p = path;
    for (const char *prefix : {"/sdcard/", "/storage/emulated/0/", "/storage/self/primary/", "/mnt/sdcard/"})
        if (p.startsWith(QLatin1String(prefix))) {
            p = p.mid(int(qstrlen(prefix)));
            break;
        }
    return p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

class MtpTransport : public Transport {
public:
    explicit MtpTransport(const QString &location)
    {
        LIBMTP_Init();
        LIBMTP_raw_device_t *raw = nullptr;
        int n = 0;
        if (LIBMTP_Detect_Raw_Devices(&raw, &n) != LIBMTP_ERROR_NONE || n == 0) {
            m_error = QStringLiteral("no MTP phone found");
            return;
        }
        for (int i = 0; i < n; ++i)
            if (QStringLiteral("%1:%2").arg(raw[i].bus_location).arg(raw[i].devnum) == location)
                m_dev = LIBMTP_Open_Raw_Device_Uncached(&raw[i]);
        free(raw);
        if (!m_dev) {
            m_error = QStringLiteral("couldn't open the phone over MTP — unlock it, choose \"File transfer\", "
                                     "and close any file manager that has it open");
            return;
        }
        LIBMTP_Get_Storage(m_dev, LIBMTP_STORAGE_SORTBY_NOTSORTED);
        m_storage = m_dev->storage ? m_dev->storage->id : 0;
        if (!m_storage) {
            LIBMTP_Clear_Errorstack(m_dev);
            // Seen on a locked Galaxy: the session opens but no storage is offered.
            m_error = QStringLiteral("the phone isn't sharing its storage over MTP — unlock it, and tap \"Allow\" "
                                     "if it asks about access to phone data");
        }
    }
    ~MtpTransport() override
    {
        if (m_dev)
            LIBMTP_Release_Device(m_dev);
    }
    bool ok() const override { return m_dev && m_storage; }
    QString error() const override { return m_error; }

    QVector<File> list(const QString &root, bool *ok) override
    {
        QVector<File> out;
        bool found = false;
        const uint32_t folder = resolveFolder(storageParts(root), false, &found);
        if (ok)
            *ok = found;
        if (found)
            walk(folder, root, &out);
        return out;
    }

    qint64 freeBytes(const QString &) override
    {
        return m_dev && m_dev->storage ? qint64(m_dev->storage->FreeSpaceInBytes) : -1;
    }

    bool push(const QString &local, const QString &remote, QString *err) override
    {
        QStringList parts = storageParts(remote);
        if (parts.isEmpty())
            return fail(err, QStringLiteral("bad phone path"));
        const QString name = parts.takeLast();
        bool found = false;
        const uint32_t parent = resolveFolder(parts, true, &found);
        if (!found)
            return fail(err, QStringLiteral("couldn't create the folder on the phone"));
        LIBMTP_file_t *meta = LIBMTP_new_file_t();
        const QByteArray n = name.toUtf8();
        meta->filename = strdup(n.constData());
        meta->filesize = uint64_t(QFileInfo(local).size());
        meta->parent_id = parent;
        meta->storage_id = m_storage;
        meta->filetype = LIBMTP_FILETYPE_UNKNOWN;
        const int rc = LIBMTP_Send_File_From_File(m_dev, QFile::encodeName(local).constData(), meta, nullptr, nullptr);
        if (rc == 0)
            m_ids.insert(remote, meta->item_id);
        LIBMTP_destroy_file_t(meta);
        return rc == 0 || fail(err, lastError());
    }

    bool pull(const QString &remote, const QString &local, QString *err) override
    {
        const uint32_t id = m_ids.value(remote);
        if (!id)
            return fail(err, QStringLiteral("not found on the phone"));
        return LIBMTP_Get_File_To_File(m_dev, id, QFile::encodeName(local).constData(), nullptr, nullptr) == 0
               || fail(err, lastError());
    }

    bool rename(const QString &from, const QString &to, QString *err) override
    {
        const uint32_t id = m_ids.value(from);
        if (!id)
            return fail(err, QStringLiteral("not found on the phone"));
        LIBMTP_file_t *f = LIBMTP_Get_Filemetadata(m_dev, id);
        if (!f)
            return fail(err, lastError());
        const QByteArray n = storageParts(to).value(storageParts(to).size() - 1).toUtf8();
        const int rc = LIBMTP_Set_File_Name(m_dev, f, n.constData());
        LIBMTP_destroy_file_t(f);
        if (rc != 0)
            return fail(err, lastError());
        m_ids.remove(from);
        m_ids.insert(to, id);
        return true;
    }

    bool remove(const QString &remote, QString *err) override
    {
        const uint32_t id = m_ids.value(remote);
        if (!id)
            return true;  // nothing there
        if (LIBMTP_Delete_Object(m_dev, id) != 0)
            return fail(err, lastError());
        m_ids.remove(remote);
        return true;
    }

private:
    bool fail(QString *err, const QString &msg)
    {
        if (err)
            *err = msg;
        return false;
    }

    QString lastError()
    {
        QString msg = QStringLiteral("MTP error");
        if (LIBMTP_error_t *e = LIBMTP_Get_Errorstack(m_dev)) {
            if (e->error_text)
                msg = QString::fromUtf8(e->error_text);
            LIBMTP_Clear_Errorstack(m_dev);
        }
        return msg;
    }

    // Folder id for a path of names under the storage root (0xFFFFFFFF = root).
    uint32_t resolveFolder(const QStringList &parts, bool create, bool *found)
    {
        uint32_t parent = LIBMTP_FILES_AND_FOLDERS_ROOT;
        QString key;
        for (const QString &name : parts) {
            key += QLatin1Char('/') + name;
            if (const auto it = m_folders.constFind(key); it != m_folders.cend()) {
                parent = *it;
                continue;
            }
            uint32_t next = 0;
            LIBMTP_file_t *files = LIBMTP_Get_Files_And_Folders(m_dev, m_storage, parent);
            for (LIBMTP_file_t *f = files; f;) {
                if (!next && f->filetype == LIBMTP_FILETYPE_FOLDER && QString::fromUtf8(f->filename) == name)
                    next = f->item_id;
                LIBMTP_file_t *gone = f;
                f = f->next;
                LIBMTP_destroy_file_t(gone);
            }
            if (!next && create) {
                QByteArray n = name.toUtf8();
                next = LIBMTP_Create_Folder(m_dev, n.data(), parent, m_storage);
            }
            if (!next) {
                *found = false;
                return 0;
            }
            m_folders.insert(key, next);
            parent = next;
        }
        *found = true;
        return parent;
    }

    void walk(uint32_t folder, const QString &path, QVector<File> *out)
    {
        LIBMTP_file_t *files = LIBMTP_Get_Files_And_Folders(m_dev, m_storage, folder);
        for (LIBMTP_file_t *f = files; f;) {
            const QString name = QString::fromUtf8(f->filename);
            const QString child = path + QLatin1Char('/') + name;
            if (!name.startsWith(QLatin1Char('.'))) {
                if (f->filetype == LIBMTP_FILETYPE_FOLDER) {
                    walk(f->item_id, child, out);
                } else {
                    out->push_back({child, qint64(f->filesize)});
                    m_ids.insert(child, f->item_id);
                }
            }
            LIBMTP_file_t *gone = f;
            f = f->next;
            LIBMTP_destroy_file_t(gone);
        }
    }

    LIBMTP_mtpdevice_t *m_dev = nullptr;
    uint32_t m_storage = 0;
    QString m_error;
    QHash<QString, uint32_t> m_ids;      // phone path → object id (from listing / pushes)
    QHash<QString, uint32_t> m_folders;  // "/Music/sub" → folder id
};
#endif

} // namespace

std::unique_ptr<Transport> Transport::open(const QString &spec)
{
    if (spec.startsWith(QLatin1String("mtp:"))) {
#ifdef CYANBEAT_HAVE_MTP
        return std::make_unique<MtpTransport>(spec.mid(4));
#else
        return nullptr;
#endif
    }
    return std::make_unique<AdbTransport>(spec.startsWith(QLatin1String("adb:")) ? spec.mid(4) : spec);
}

namespace Mtp {

QVector<Device> devicesCached(const QHash<QString, QPair<QString, QString>> &cache, QString *error);

bool available()
{
#ifdef CYANBEAT_HAVE_MTP
    return true;
#else
    return false;
#endif
}

QVector<Device> devices(QString *error)
{
    return devicesCached({}, error);
}

QVector<Device> devicesCached(const QHash<QString, QPair<QString, QString>> &cache)
{
    return devicesCached(cache, nullptr);
}

QVector<Device> devicesCached(const QHash<QString, QPair<QString, QString>> &cache, QString *error)
{
    QVector<Device> out;
#ifdef CYANBEAT_HAVE_MTP
    LIBMTP_Init();
    LIBMTP_raw_device_t *raw = nullptr;
    int n = 0;
    const LIBMTP_error_number_t rc = LIBMTP_Detect_Raw_Devices(&raw, &n);
    if (rc != LIBMTP_ERROR_NONE) {
        if (error && rc != LIBMTP_ERROR_NO_DEVICE_ATTACHED)
            *error = QStringLiteral("MTP detection failed (%1)").arg(int(rc));
        return out;
    }
    for (int i = 0; i < n; ++i) {
        Device d;
        d.location = QStringLiteral("%1:%2").arg(raw[i].bus_location).arg(raw[i].devnum);
        if (const auto hit = cache.constFind(d.location); hit != cache.cend()) {
            d.serial = hit->first;
            d.model = hit->second;
            out << d;
            continue;
        }
        if (LIBMTP_mtpdevice_t *dev = LIBMTP_Open_Raw_Device_Uncached(&raw[i])) {
            if (char *s = LIBMTP_Get_Serialnumber(dev)) {
                d.serial = QString::fromUtf8(s).trimmed();
                free(s);
            }
            if (char *m = LIBMTP_Get_Modelname(dev)) {
                d.model = QString::fromUtf8(m).trimmed();
                free(m);
            }
            LIBMTP_Release_Device(dev);
        } else {
            d.model = QString::fromUtf8(raw[i].device_entry.product ? raw[i].device_entry.product : "MTP device");
        }
        out << d;
    }
    free(raw);
#else
    Q_UNUSED(cache);
    Q_UNUSED(error);
#endif
    return out;
}

} // namespace Mtp
