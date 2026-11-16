#include "reportmanager.h"

#include "settingsmanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace {

QString reportsDirectory(const QString& base)
{
    return base + QStringLiteral("/reports");
}

QString attachmentsDirectory(const QString& base)
{
    return base + QStringLiteral("/attachments");
}

QString timeString(qint64 ms)
{
    if (ms <= 0)
        ms = QDateTime::currentMSecsSinceEpoch();
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm"));
}

QString dateTimeString(qint64 ms)
{
    if (ms <= 0)
        ms = QDateTime::currentMSecsSinceEpoch();
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

qint64 parseCreatedTs(const QString& created)
{
    const QDateTime dt = QDateTime::fromString(created, QStringLiteral("dd.MM.yyyy HH:mm"));
    return dt.isValid() ? dt.toMSecsSinceEpoch() : 0;
}

QString makeMessageId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString sanitizeFileName(const QString& name)
{
    return QFileInfo(name).fileName();
}

} // namespace

// =====================================================================
//  Report helpers
// =====================================================================
bool Report::isParticipant(const QString& id) const
{
    if (id.isEmpty())
        return false;
    for (const Participant& p : participants) {
        if (p.id == id)
            return true;
    }
    return false;
}

QString Report::displayTitle(const QString& id) const
{
    if (kind == QLatin1String("direct")) {
        for (const Participant& p : participants) {
            if (p.id != id && !p.name.trimmed().isEmpty())
                return p.name.trimmed();
        }
        for (const Participant& p : participants) {
            if (p.id != id && !p.id.isEmpty())
                return p.id.left(8);
        }
    }
    if (!title.trimmed().isEmpty())
        return title.trimmed();
    return kind == QLatin1String("direct") ? QStringLiteral("Личный чат")
                                           : QStringLiteral("Групповой чат");
}

QString Report::participantSummary(const QString& id) const
{
    QStringList names;
    for (const Participant& p : participants) {
        if (p.id == id)
            continue;
        names << (p.name.trimmed().isEmpty() ? p.id.left(8) : p.name.trimmed());
    }
    if (names.isEmpty())
        return {};
    if (names.size() > 4)
        return names.mid(0, 4).join(QStringLiteral(", ")) + QStringLiteral("…");
    return names.join(QStringLiteral(", "));
}

int Report::unreadIncomingCount(const QString& id) const
{
    int count = 0;
    for (const Message& m : messages) {
        if (!m.isRead && !m.senderId.isEmpty() && m.senderId != id)
            ++count;
    }
    return count;
}

// =====================================================================
//  JSON helpers
// =====================================================================
QList<Participant> ReportManager::participantsFromJson(const QJsonArray& array)
{
    QList<Participant> result;
    for (const QJsonValue& value : array) {
        if (value.isString()) {
            result.push_back({value.toString(), QString()});
            continue;
        }
        const QJsonObject o = value.toObject();
        Participant p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.name = o.value(QStringLiteral("name")).toString();
        if (!p.id.isEmpty())
            result << p;
    }
    return result;
}

QJsonArray ReportManager::participantsToJson(const QList<Participant>& participants)
{
    QJsonArray array;
    for (const Participant& p : participants) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), p.id);
        o.insert(QStringLiteral("name"), p.name);
        array.append(o);
    }
    return array;
}

QJsonObject ReportManager::attachmentToJson(const Attachment& a)
{
    QJsonObject o;
    o.insert(QStringLiteral("filename"), a.filename);
    o.insert(QStringLiteral("displayName"), a.displayName);
    o.insert(QStringLiteral("size"), a.size);
    o.insert(QStringLiteral("ownerId"), a.ownerId);
    o.insert(QStringLiteral("ownerName"), a.ownerName);
    o.insert(QStringLiteral("downloaded"), a.downloaded);
    o.insert(QStringLiteral("messageId"), a.messageId);
    return o;
}

Attachment ReportManager::attachmentFromJson(const QJsonObject& o)
{
    Attachment a;
    a.filename = sanitizeFileName(o.value(QStringLiteral("filename")).toString());
    a.displayName = o.value(QStringLiteral("displayName")).toString().trimmed();
    if (a.displayName.isEmpty())
        a.displayName = a.filename;
    a.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
    a.ownerId = o.value(QStringLiteral("ownerId")).toString();
    a.ownerName = o.value(QStringLiteral("ownerName")).toString();
    a.downloaded = o.value(QStringLiteral("downloaded")).toBool(true);
    a.messageId = o.value(QStringLiteral("messageId")).toString();
    return a;
}

QJsonObject ReportManager::messageToJson(const Report& r, const Message& m)
{
    Q_UNUSED(r)
    QJsonObject o;
    o.insert(QStringLiteral("id"), m.id);
    o.insert(QStringLiteral("senderId"), m.senderId);
    o.insert(QStringLiteral("senderName"), m.senderName);
    o.insert(QStringLiteral("text"), m.text);
    o.insert(QStringLiteral("ts"), m.ts);
    o.insert(QStringLiteral("timestamp"), m.timestamp);
    o.insert(QStringLiteral("isRead"), m.isRead);

    QJsonArray attachments;
    for (const Attachment& a : m.attachments)
        attachments.append(attachmentToJson(a));
    o.insert(QStringLiteral("attachments"), attachments);
    return o;
}

QJsonObject ReportManager::conversationToJson(const Report& r)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), r.id);
    o.insert(QStringLiteral("title"), r.title);
    o.insert(QStringLiteral("description"), r.description);
    o.insert(QStringLiteral("author"), r.author);
    o.insert(QStringLiteral("authorId"), r.authorId);
    o.insert(QStringLiteral("created"), r.created);
    o.insert(QStringLiteral("createdTs"), r.createdTs);
    o.insert(QStringLiteral("lastActivityTs"), r.lastActivityTs);
    o.insert(QStringLiteral("status"), r.status);
    o.insert(QStringLiteral("archived"), r.status == QLatin1String("resolved"));
    o.insert(QStringLiteral("kind"), r.kind);
    o.insert(QStringLiteral("participants"), participantsToJson(r.participants));

    QJsonArray attachments;
    for (const Attachment& a : r.attachments) {
        if (!a.filename.isEmpty())
            attachments.append(attachmentToJson(a));
    }
    o.insert(QStringLiteral("attachments"), attachments);

    QJsonArray messages;
    for (const Message& m : r.messages)
        messages.append(messageToJson(r, m));
    o.insert(QStringLiteral("messages"), messages);
    return o;
}

// =====================================================================
//  Construction / DB
// =====================================================================
ReportManager::ReportManager(const QString& basePath)
    : m_base(basePath.isEmpty() ? QDir::homePath() + QStringLiteral("/SupportIL") : basePath)
{
    m_selfId = SettingsManager::instance().networkId();
    m_selfName = SettingsManager::instance().pcName();
    openDatabase();
}

void ReportManager::setIdentity(const QString& id, const QString& name)
{
    m_selfId = id.trimmed().isEmpty() ? SettingsManager::instance().networkId()
                                      : id.trimmed();
    m_selfName = name.trimmed().isEmpty() ? SettingsManager::instance().pcName()
                                          : name.trimmed();
}

ReportManager::~ReportManager()
{
    closeDatabase();
}

QString ReportManager::reportsDir() const
{
    return reportsDirectory(m_base);
}

QString ReportManager::attachmentsDir() const
{
    return attachmentsDirectory(m_base);
}

QString ReportManager::dbPath() const
{
    return m_base + QStringLiteral("/supportil.db");
}

void ReportManager::setBasePath(const QString& path)
{
    const QString normalized = path.isEmpty()
                                   ? QDir::homePath() + QStringLiteral("/SupportIL")
                                   : path;
    if (normalized == m_base && m_db.isOpen())
        return;

    closeDatabase();
    m_base = normalized;
    openDatabase();
    bumpRevision();
}

bool ReportManager::openDatabase()
{
    if (!QDir().mkpath(m_base) || !QDir().mkpath(attachmentsDir())) {
        m_lastError = QStringLiteral("Не удалось создать папку данных:\n%1").arg(m_base);
        return false;
    }

    m_connectionName = QStringLiteral("supportil_reports_%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath());
    m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));

    if (!m_db.open()) {
        m_lastError = QStringLiteral("Не удалось открыть базу чатов:\n%1")
                          .arg(m_db.lastError().text());
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode=DELETE"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));

    if (!ensureSchema())
        return false;

    migrateLegacyJson();
    m_lastError.clear();
    return true;
}

void ReportManager::closeDatabase()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
    }
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
}

bool ReportManager::ensureColumn(const QString& table, const QString& column,
                                 const QString& definition)
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0)
            return true;
    }
    QSqlQuery alter(m_db);
    return alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                          .arg(table, column, definition));
}

bool ReportManager::ensureSchema()
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS reports ("
            "  id TEXT PRIMARY KEY,"
            "  title TEXT NOT NULL DEFAULT '',"
            "  description TEXT NOT NULL DEFAULT '',"
            "  author TEXT NOT NULL DEFAULT '',"
            "  author_id TEXT NOT NULL DEFAULT '',"
            "  created TEXT NOT NULL DEFAULT '',"
            "  created_ts INTEGER NOT NULL DEFAULT 0,"
            "  last_activity_ts INTEGER NOT NULL DEFAULT 0,"
            "  status TEXT NOT NULL DEFAULT 'open',"
            "  archived INTEGER NOT NULL DEFAULT 0,"
            "  deleted INTEGER NOT NULL DEFAULT 0,"
            "  kind TEXT NOT NULL DEFAULT '',"
            "  participants TEXT NOT NULL DEFAULT ''"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id TEXT PRIMARY KEY,"
            "  report_id TEXT NOT NULL,"
            "  sender_id TEXT NOT NULL DEFAULT '',"
            "  sender_name TEXT NOT NULL DEFAULT '',"
            "  role TEXT NOT NULL DEFAULT 'user',"
            "  text TEXT NOT NULL DEFAULT '',"
            "  ts TEXT NOT NULL DEFAULT '',"
            "  timestamp INTEGER NOT NULL DEFAULT 0,"
            "  is_read INTEGER NOT NULL DEFAULT 1,"
            "  FOREIGN KEY (report_id) REFERENCES reports(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS attachments ("
            "  report_id TEXT NOT NULL,"
            "  message_id TEXT NOT NULL DEFAULT '',"
            "  filename TEXT NOT NULL,"
            "  display_name TEXT NOT NULL DEFAULT '',"
            "  size INTEGER NOT NULL DEFAULT 0,"
            "  owner_id TEXT NOT NULL DEFAULT '',"
            "  owner_name TEXT NOT NULL DEFAULT '',"
            "  downloaded INTEGER NOT NULL DEFAULT 1,"
            "  PRIMARY KEY (report_id, message_id, filename),"
            "  FOREIGN KEY (report_id) REFERENCES reports(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS meta ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT"
            ")")
    };

    QSqlQuery q(m_db);
    for (const QString& sql : statements) {
        if (!q.exec(sql)) {
            setError(q, QStringLiteral("Ошибка инициализации базы чатов"));
            return false;
        }
    }

    // Миграция старой схемы.
    ensureColumn(QStringLiteral("reports"), QStringLiteral("author_id"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("reports"), QStringLiteral("last_activity_ts"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("reports"), QStringLiteral("archived"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("reports"), QStringLiteral("deleted"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("reports"), QStringLiteral("kind"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("reports"), QStringLiteral("participants"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));

    ensureColumn(QStringLiteral("messages"), QStringLiteral("sender_id"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("messages"), QStringLiteral("sender_name"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("messages"), QStringLiteral("timestamp"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("messages"), QStringLiteral("role"), QStringLiteral("TEXT NOT NULL DEFAULT 'user'"));

    ensureColumn(QStringLiteral("attachments"), QStringLiteral("message_id"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("attachments"), QStringLiteral("display_name"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("attachments"), QStringLiteral("size"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("attachments"), QStringLiteral("owner_id"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("attachments"), QStringLiteral("owner_name"), QStringLiteral("TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("attachments"), QStringLiteral("downloaded"), QStringLiteral("INTEGER NOT NULL DEFAULT 1"));

    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_report ON messages(report_id, timestamp)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_unread ON messages(report_id, is_read, sender_id)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_reports_status ON reports(status, archived, deleted, last_activity_ts DESC)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_attachments_report ON attachments(report_id, message_id)"));

    q.exec(QStringLiteral("UPDATE reports SET last_activity_ts = created_ts WHERE last_activity_ts = 0"));

    // Старые баг-репорты были общими и не имели участников. Чтобы не
    // смешивать их с новым мессенджером, переносим их в архив. Операция
    // идемпотентна: чаты мессенджера всегда имеют список участников.
    q.exec(QStringLiteral(
        "UPDATE reports SET archived = 1, status = 'resolved'"
        " WHERE (participants = '' OR participants IS NULL) AND archived = 0"));
    setMeta(QStringLiteral("messenger_migrated"), QStringLiteral("1"));

    return true;
}

void ReportManager::migrateLegacyJson()
{
    if (meta(QStringLiteral("legacy_migrated")) == QLatin1String("1"))
        return;

    QDir dir(reportsDir());
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    if (files.isEmpty()) {
        setMeta(QStringLiteral("legacy_migrated"), QStringLiteral("1"));
        return;
    }

    for (const QString& name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();

        Report r = fromJson(object);
        if (r.id.isEmpty())
            r.id = QFileInfo(name).completeBaseName();
        if (r.id.isEmpty())
            continue;

        bool isNew = false;
        importConversation(conversationToJson(r), isNew);
    }

    setMeta(QStringLiteral("legacy_migrated"), QStringLiteral("1"));
}

// =====================================================================
//  Chat creation
// =====================================================================
QString ReportManager::directChatId(const QString& a, const QString& b)
{
    QStringList ids{a, b};
    std::sort(ids.begin(), ids.end());
    const QByteArray hash = QCryptographicHash::hash(
        ids.join(QLatin1Char('|')).toUtf8(), QCryptographicHash::Sha1);
    return QStringLiteral("d-") + QString::fromLatin1(hash.toHex().left(20));
}

QString ReportManager::ensureDirectChat(const QString& peerId, const QString& peerName)
{
    if (!m_db.isOpen() || peerId.isEmpty() || peerId == selfId())
        return {};

    const QString id = directChatId(selfId(), peerId);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString participants =
        QString::fromUtf8(QJsonDocument(participantsToJson({
            Participant{selfId(), selfName()},
            {peerId, peerName.trimmed().isEmpty() ? peerId.left(8) : peerName.trimmed()}
        })).toJson(QJsonDocument::Compact));

    QSqlQuery exists(m_db);
    exists.prepare(QStringLiteral("SELECT title FROM reports WHERE id = ?"));
    exists.addBindValue(id);
    if (exists.exec() && exists.next()) {
        QSqlQuery upd(m_db);
        upd.prepare(QStringLiteral(
            "UPDATE reports SET kind = 'direct', participants = ?, archived = 0,"
            " deleted = 0, status = 'open' WHERE id = ?"));
        upd.addBindValue(participants);
        upd.addBindValue(id);
        if (!upd.exec())
            return {};
        bumpRevision();
        return id;
    }

    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral(
        "INSERT INTO reports"
        " (id, title, description, author, author_id, created, created_ts,"
        "  last_activity_ts, status, archived, deleted, kind, participants)"
        " VALUES (?, ?, '', ?, ?, ?, ?, ?, 'open', 0, 0, 'direct', ?)"));
    ins.addBindValue(id);
    ins.addBindValue(peerName.trimmed().isEmpty() ? peerId.left(8) : peerName.trimmed());
    ins.addBindValue(selfName());
    ins.addBindValue(selfId());
    ins.addBindValue(dateTimeString(nowMs));
    ins.addBindValue(nowMs);
    ins.addBindValue(nowMs);
    ins.addBindValue(participants);
    if (!ins.exec()) {
        setError(ins, QStringLiteral("Не удалось создать личный чат"));
        return {};
    }

    bumpRevision();
    return id;
}

QString ReportManager::createGroupChat(const QString& title,
                                       const QList<Participant>& inputParticipants)
{
    if (!m_db.isOpen())
        return {};

    QList<Participant> participants = inputParticipants;
    bool hasSelf = false;
    for (const Participant& p : participants) {
        if (p.id == selfId()) {
            hasSelf = true;
            break;
        }
    }
    if (!hasSelf)
        participants.prepend(Participant{selfId(), selfName()});

    if (participants.size() < 2) {
        m_lastError = QStringLiteral("В группе должен быть хотя бы один участник.");
        return {};
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString id = QStringLiteral("g-")
                       + QDateTime::fromMSecsSinceEpoch(nowMs)
                             .toString(QStringLiteral("yyyyMMdd-HHmmss-"))
                       + QUuid::createUuid().toString(QUuid::WithoutBraces).left(6);

    const QString safeTitle = title.trimmed().isEmpty()
                                  ? QStringLiteral("Группа %1").arg(dateTimeString(nowMs))
                                  : title.trimmed();

    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral(
        "INSERT INTO reports"
        " (id, title, description, author, author_id, created, created_ts,"
        "  last_activity_ts, status, archived, deleted, kind, participants)"
        " VALUES (?, ?, '', ?, ?, ?, ?, ?, 'open', 0, 0, 'group', ?)"));
    ins.addBindValue(id);
    ins.addBindValue(safeTitle);
    ins.addBindValue(selfName());
    ins.addBindValue(selfId());
    ins.addBindValue(dateTimeString(nowMs));
    ins.addBindValue(nowMs);
    ins.addBindValue(nowMs);
    ins.addBindValue(QString::fromUtf8(
        QJsonDocument(participantsToJson(participants)).toJson(QJsonDocument::Compact)));
    if (!ins.exec()) {
        setError(ins, QStringLiteral("Не удалось создать групповой чат"));
        return {};
    }

    bumpRevision();
    return id;
}

bool ReportManager::addParticipants(const QString& id,
                                    const QList<Participant>& people,
                                    QJsonObject& conversation)
{
    conversation = QJsonObject();
    if (!m_db.isOpen() || id.isEmpty())
        return false;

    Report report = loadOne(id);
    if (report.id.isEmpty() || report.kind != QLatin1String("group"))
        return false;

    QSet<QString> known;
    for (const Participant& p : report.participants)
        known.insert(p.id);

    bool changed = false;
    for (const Participant& p : people) {
        if (p.id.isEmpty() || p.id == selfId() || known.contains(p.id))
            continue;
        Participant added = p;
        added.name = p.name.trimmed().isEmpty() ? p.id.left(8) : p.name.trimmed();
        report.participants << added;
        known.insert(p.id);
        changed = true;
    }

    if (changed) {
        QSqlQuery upd(m_db);
        upd.prepare(QStringLiteral("UPDATE reports SET participants = ? WHERE id = ?"));
        upd.addBindValue(QString::fromUtf8(
            QJsonDocument(participantsToJson(report.participants))
                .toJson(QJsonDocument::Compact)));
        upd.addBindValue(id);
        if (!upd.exec()) {
            setError(upd, QStringLiteral("Не удалось добавить участников"));
            return false;
        }
        bumpRevision();
    }

    conversation = exportConversation(id);
    return !conversation.isEmpty();
}

// =====================================================================
//  Sending / importing
// =====================================================================
bool ReportManager::sendLocalMessage(const QString& id, const QString& text,
                                     const QList<QString>& files, QJsonObject& payload,
                                     Message& outMessage)
{
    payload = QJsonObject();
    outMessage = Message();
    if (!m_db.isOpen() || id.isEmpty())
        return false;

    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty() && files.isEmpty())
        return false;

    const Report existing = loadOne(id);
    if (existing.id.isEmpty())
        return false;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    outMessage.id = makeMessageId();
    outMessage.senderId = selfId();
    outMessage.senderName = selfName();
    outMessage.text = trimmedText;
    outMessage.timestamp = nowMs;
    outMessage.ts = timeString(nowMs);
    outMessage.isRead = true;

    QDir().mkpath(attachmentsDir());
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo srcInfo(files.at(i));
        if (!srcInfo.exists() || !srcInfo.isFile())
            continue;

        const QString safeName = QStringLiteral("%1_%2_%3")
                                     .arg(id.left(8), outMessage.id.left(8),
                                          sanitizeFileName(srcInfo.fileName()));
        const QString destination = attachmentsDir() + QLatin1Char('/') + safeName;
        QFile::remove(destination);
        if (!QFile::copy(srcInfo.absoluteFilePath(), destination))
            continue;

        Attachment a;
        a.filename = safeName;
        a.displayName = srcInfo.fileName();
        a.size = srcInfo.size();
        a.ownerId = selfId();
        a.ownerName = selfName();
        a.downloaded = true;
        a.messageId = outMessage.id;
        outMessage.attachments << a;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QSqlQuery insMsg(m_db);
    insMsg.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO messages"
        " (id, report_id, sender_id, sender_name, role, text, ts, timestamp, is_read)"
        " VALUES (?, ?, ?, ?, 'user', ?, ?, ?, 1)"));
    insMsg.addBindValue(outMessage.id);
    insMsg.addBindValue(id);
    insMsg.addBindValue(outMessage.senderId);
    insMsg.addBindValue(outMessage.senderName);
    insMsg.addBindValue(outMessage.text);
    insMsg.addBindValue(outMessage.ts);
    insMsg.addBindValue(outMessage.timestamp);
    if (!insMsg.exec()) {
        setError(insMsg, QStringLiteral("Не удалось сохранить сообщение"));
        m_db.rollback();
        return false;
    }

    QSqlQuery insAtt(m_db);
    insAtt.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO attachments"
        " (report_id, message_id, filename, display_name, size, owner_id, owner_name, downloaded)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const Attachment& a : outMessage.attachments) {
        insAtt.addBindValue(id);
        insAtt.addBindValue(outMessage.id);
        insAtt.addBindValue(a.filename);
        insAtt.addBindValue(a.visibleName());
        insAtt.addBindValue(a.size);
        insAtt.addBindValue(a.ownerId);
        insAtt.addBindValue(a.ownerName);
        insAtt.addBindValue(a.downloaded ? 1 : 0);
        insAtt.exec();
    }

    QSqlQuery updReport(m_db);
    updReport.prepare(QStringLiteral(
        "UPDATE reports SET last_activity_ts = ?, status = 'open', archived = 0,"
        " deleted = 0 WHERE id = ?"));
    updReport.addBindValue(outMessage.timestamp);
    updReport.addBindValue(id);
    updReport.exec();

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    bumpRevision();

    const Report report = loadOne(id);
    payload.insert(QStringLiteral("type"), QStringLiteral("message"));
    payload.insert(QStringLiteral("conversation"), conversationToJson(report));
    payload.insert(QStringLiteral("message"), messageToJson(report, outMessage));
    return true;
}

bool ReportManager::importConversation(const QJsonObject& conversation, bool& isNew)
{
    isNew = false;
    if (!m_db.isOpen() || conversation.isEmpty())
        return false;

    const QString id = conversation.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty())
        return false;

    const QString title = conversation.value(QStringLiteral("title")).toString();
    const QString description = conversation.value(QStringLiteral("description")).toString();
    const QString author = conversation.value(QStringLiteral("author")).toString();
    const QString authorId = conversation.value(QStringLiteral("authorId")).toString();
    const QString created = conversation.value(QStringLiteral("created")).toString();
    const qint64 createdTs = conversation.value(QStringLiteral("createdTs")).toVariant().toLongLong();
    const qint64 lastActivityTs = conversation.value(QStringLiteral("lastActivityTs")).toVariant().toLongLong();
    const QString status = conversation.value(QStringLiteral("status")).toString(QStringLiteral("open"));
    const bool archived = conversation.value(QStringLiteral("archived")).toBool(status == QLatin1String("resolved"));
    const QString kind = conversation.value(QStringLiteral("kind")).toString();
    QList<Participant> participants =
        participantsFromJson(conversation.value(QStringLiteral("participants")).toArray());

    // Приватность: не принимаем чужие личные/групповые чаты.
    if (!participants.isEmpty()) {
        bool mine = false;
        for (const Participant& p : participants) {
            if (p.id == selfId()) {
                mine = true;
                break;
            }
        }
        if (!mine)
            return false;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QSqlQuery exists(m_db);
    exists.prepare(QStringLiteral("SELECT kind, participants FROM reports WHERE id = ?"));
    exists.addBindValue(id);
    const bool known = exists.exec() && exists.next();

    if (known) {
        // Удаление участников не поддерживается, поэтому составы групп
        // объединяем: сообщение от пользователя со старой копией состава
        // не должно «терять» недавно добавленных участников.
        const QString storedKind = exists.value(0).toString();
        if (storedKind == QLatin1String("group")) {
            const QJsonArray storedArray =
                QJsonDocument::fromJson(exists.value(1).toString().toUtf8()).array();
            QList<Participant> merged = participantsFromJson(storedArray);
            QSet<QString> seen;
            for (const Participant& p : merged)
                seen.insert(p.id);
            for (const Participant& p : participants) {
                if (p.id.isEmpty())
                    continue;
                if (!seen.contains(p.id)) {
                    merged << p;
                    seen.insert(p.id);
                    continue;
                }
                for (Participant& existing : merged) {
                    if (existing.id == p.id && !p.name.trimmed().isEmpty())
                        existing.name = p.name;
                }
            }
            participants = merged;
        }
    }

    const QString participantsJson = QString::fromUtf8(
        QJsonDocument(participantsToJson(participants)).toJson(QJsonDocument::Compact));

    if (known) {
        QSqlQuery upd(m_db);
        upd.prepare(QStringLiteral(
            "UPDATE reports SET title = ?, description = ?, author = ?, author_id = ?,"
            " created = ?, created_ts = CASE WHEN ? > 0 THEN ? ELSE created_ts END,"
            " last_activity_ts = CASE WHEN ? > last_activity_ts THEN ? ELSE last_activity_ts END,"
            " deleted = 0, kind = ?, participants = ?"
            " WHERE id = ?"));
        upd.addBindValue(title);
        upd.addBindValue(description);
        upd.addBindValue(author);
        upd.addBindValue(authorId);
        upd.addBindValue(created);
        upd.addBindValue(createdTs);
        upd.addBindValue(createdTs);
        upd.addBindValue(lastActivityTs);
        upd.addBindValue(lastActivityTs);
        upd.addBindValue(kind);
        upd.addBindValue(participantsJson);
        upd.addBindValue(id);
        if (!upd.exec()) {
            setError(upd, QStringLiteral("Не удалось обновить чат"));
            m_db.rollback();
            return false;
        }
    } else {
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT INTO reports"
            " (id, title, description, author, author_id, created, created_ts,"
            "  last_activity_ts, status, archived, deleted, kind, participants)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?)"));
        ins.addBindValue(id);
        ins.addBindValue(title);
        ins.addBindValue(description);
        ins.addBindValue(author);
        ins.addBindValue(authorId);
        ins.addBindValue(created);
        ins.addBindValue(createdTs);
        ins.addBindValue(lastActivityTs > 0 ? lastActivityTs : createdTs);
        ins.addBindValue(status);
        ins.addBindValue(archived ? 1 : 0);
        ins.addBindValue(kind);
        ins.addBindValue(participantsJson);
        if (!ins.exec()) {
            setError(ins, QStringLiteral("Не удалось импортировать чат"));
            m_db.rollback();
            return false;
        }
        isNew = true;
    }

    // Вложения, не привязанные к сообщению.
    QSqlQuery insAttach(m_db);
    insAttach.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO attachments"
        " (report_id, message_id, filename, display_name, size, owner_id, owner_name, downloaded)"
        " VALUES (?, '', ?, ?, ?, ?, ?, ?)"));
    for (const QJsonValue& value : conversation.value(QStringLiteral("attachments")).toArray()) {
        const QJsonObject ao = value.toObject();
        const QString fileName = sanitizeFileName(ao.value(QStringLiteral("filename")).toString());
        if (fileName.isEmpty())
            continue;
        const QString displayName = ao.value(QStringLiteral("displayName")).toString();
        insAttach.addBindValue(id);
        insAttach.addBindValue(fileName);
        insAttach.addBindValue(displayName.isEmpty() ? fileName : displayName);
        insAttach.addBindValue(ao.value(QStringLiteral("size")).toVariant().toLongLong());
        insAttach.addBindValue(ao.value(QStringLiteral("ownerId")).toString(authorId));
        insAttach.addBindValue(ao.value(QStringLiteral("ownerName")).toString(author));
        insAttach.addBindValue(QFileInfo::exists(attachmentsDir() + QLatin1Char('/') + fileName) ? 1 : 0);
        insAttach.exec();
    }

    QSqlQuery insMsg(m_db);
    insMsg.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO messages"
        " (id, report_id, sender_id, sender_name, role, text, ts, timestamp, is_read)"
        " VALUES (?, ?, ?, ?, 'user', ?, ?, ?, ?)"));
    QSqlQuery insMsgAttach(m_db);
    insMsgAttach.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO attachments"
        " (report_id, message_id, filename, display_name, size, owner_id, owner_name, downloaded)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));

    bool newIncoming = false;
    for (const QJsonValue& value : conversation.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject mo = value.toObject();
        const QString msgId = mo.value(QStringLiteral("id")).toString();
        if (msgId.isEmpty())
            continue;

        const QString senderId = mo.value(QStringLiteral("senderId")).toString();
        const qint64 timestamp = mo.value(QStringLiteral("timestamp")).toVariant().toLongLong();
        const bool incoming = !senderId.isEmpty() && senderId != selfId();
        // Новое входящее сообщение всегда непрочитано, независимо от того,
        // как его отметил отправитель у себя.
        const bool isRead = incoming ? false
                                     : mo.value(QStringLiteral("isRead")).toBool(true);

        insMsg.addBindValue(msgId);
        insMsg.addBindValue(id);
        insMsg.addBindValue(senderId);
        insMsg.addBindValue(mo.value(QStringLiteral("senderName")).toString());
        insMsg.addBindValue(mo.value(QStringLiteral("text")).toString());
        insMsg.addBindValue(timestamp > 0 ? timeString(timestamp)
                                          : mo.value(QStringLiteral("ts")).toString());
        insMsg.addBindValue(timestamp);
        insMsg.addBindValue(isRead ? 1 : 0);
        if (insMsg.exec() && insMsg.numRowsAffected() > 0) {
            isNew = true;
            if (incoming)
                newIncoming = true;
        }

        for (const QJsonValue& av : mo.value(QStringLiteral("attachments")).toArray()) {
            const QJsonObject ao = av.toObject();
            const QString fileName = sanitizeFileName(ao.value(QStringLiteral("filename")).toString());
            if (fileName.isEmpty())
                continue;
            const bool downloaded = QFileInfo::exists(attachmentsDir() + QLatin1Char('/') + fileName);
            const QString displayName = ao.value(QStringLiteral("displayName")).toString();
            insMsgAttach.addBindValue(id);
            insMsgAttach.addBindValue(msgId);
            insMsgAttach.addBindValue(fileName);
            insMsgAttach.addBindValue(displayName.isEmpty() ? fileName : displayName);
            insMsgAttach.addBindValue(ao.value(QStringLiteral("size")).toVariant().toLongLong());
            insMsgAttach.addBindValue(ao.value(QStringLiteral("ownerId")).toString(senderId));
            insMsgAttach.addBindValue(ao.value(QStringLiteral("ownerName")).toString());
            insMsgAttach.addBindValue(downloaded ? 1 : 0);
            insMsgAttach.exec();
        }
    }

    // Новое входящее сообщение возвращает чат из архива в активные,
    // но синхронизация истории сама по себе чат не разархивирует.
    if (newIncoming) {
        QSqlQuery unarchive(m_db);
        unarchive.prepare(QStringLiteral(
            "UPDATE reports SET archived = 0, status = 'open' WHERE id = ?"));
        unarchive.addBindValue(id);
        unarchive.exec();
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    if (isNew)
        bumpRevision();
    return true;
}

bool ReportManager::importNetworkMessage(const QJsonObject& payload, bool& isNew)
{
    isNew = false;
    if (!m_db.isOpen() || payload.isEmpty())
        return false;

    const QJsonObject conversation = payload.value(QStringLiteral("conversation")).toObject();
    const QJsonObject message = payload.value(QStringLiteral("message")).toObject();
    if (conversation.isEmpty())
        return false;

    bool convNew = false;
    if (!importConversation(conversation, convNew))
        return false;

    if (message.isEmpty()) {
        isNew = convNew;
        return true;
    }

    const QString reportId = conversation.value(QStringLiteral("id")).toString();
    const QString msgId = message.value(QStringLiteral("id")).toString();
    if (reportId.isEmpty() || msgId.isEmpty())
        return true;

    QSqlQuery check(m_db);
    check.prepare(QStringLiteral("SELECT 1 FROM messages WHERE id = ?"));
    check.addBindValue(msgId);
    if (check.exec() && check.next()) {
        isNew = convNew;
        return true;
    }

    const QString senderId = message.value(QStringLiteral("senderId")).toString();
    const bool incoming = !senderId.isEmpty() && senderId != selfId();
    const qint64 timestamp = message.value(QStringLiteral("timestamp")).toVariant().toLongLong();

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QSqlQuery insMsg(m_db);
    insMsg.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO messages"
        " (id, report_id, sender_id, sender_name, role, text, ts, timestamp, is_read)"
        " VALUES (?, ?, ?, ?, 'user', ?, ?, ?, ?)"));
    insMsg.addBindValue(msgId);
    insMsg.addBindValue(reportId);
    insMsg.addBindValue(senderId);
    insMsg.addBindValue(message.value(QStringLiteral("senderName")).toString());
    insMsg.addBindValue(message.value(QStringLiteral("text")).toString());
    insMsg.addBindValue(timestamp > 0 ? timeString(timestamp)
                                      : message.value(QStringLiteral("ts")).toString());
    insMsg.addBindValue(timestamp);
    insMsg.addBindValue(incoming ? 0 : 1);
    if (!insMsg.exec()) {
        setError(insMsg, QStringLiteral("Не удалось импортировать сообщение"));
        m_db.rollback();
        return false;
    }
    isNew = insMsg.numRowsAffected() > 0 || convNew;

    QSqlQuery insAtt(m_db);
    insAtt.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO attachments"
        " (report_id, message_id, filename, display_name, size, owner_id, owner_name, downloaded)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const QJsonValue& av : message.value(QStringLiteral("attachments")).toArray()) {
        const QJsonObject ao = av.toObject();
        const QString fileName = sanitizeFileName(ao.value(QStringLiteral("filename")).toString());
        if (fileName.isEmpty())
            continue;
        const bool downloaded = QFileInfo::exists(attachmentsDir() + QLatin1Char('/') + fileName);
        const QString displayName = ao.value(QStringLiteral("displayName")).toString();
        insAtt.addBindValue(reportId);
        insAtt.addBindValue(msgId);
        insAtt.addBindValue(fileName);
        insAtt.addBindValue(displayName.isEmpty() ? fileName : displayName);
        insAtt.addBindValue(ao.value(QStringLiteral("size")).toVariant().toLongLong());
        insAtt.addBindValue(ao.value(QStringLiteral("ownerId")).toString(senderId));
        insAtt.addBindValue(ao.value(QStringLiteral("ownerName")).toString());
        insAtt.addBindValue(downloaded ? 1 : 0);
        insAtt.exec();
    }

    QSqlQuery upd(m_db);
    upd.prepare(QStringLiteral(
        "UPDATE reports SET last_activity_ts = CASE WHEN ? > last_activity_ts THEN ? ELSE last_activity_ts END,"
        " status = 'open', archived = 0, deleted = 0 WHERE id = ?"));
    upd.addBindValue(timestamp);
    upd.addBindValue(timestamp);
    upd.addBindValue(reportId);
    upd.exec();

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    if (isNew)
        bumpRevision();
    return true;
}

// =====================================================================
//  Loading
// =====================================================================
QList<Report> ReportManager::loadAll() const
{
    QList<Report> reports;
    if (!m_db.isOpen())
        return reports;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT id, title, description, author, author_id, created, created_ts,"
            "       last_activity_ts, status, archived, deleted, kind, participants"
            "  FROM reports WHERE deleted = 0"
            " ORDER BY last_activity_ts DESC, created_ts DESC, id DESC"))) {
        setError(query, QStringLiteral("Не удалось загрузить чаты"));
        return reports;
    }

    QHash<QString, int> index;
    while (query.next()) {
        Report r;
        r.id = query.value(0).toString();
        r.title = query.value(1).toString();
        r.description = query.value(2).toString();
        r.author = query.value(3).toString();
        r.authorId = query.value(4).toString();
        r.created = query.value(5).toString();
        r.createdTs = query.value(6).toLongLong();
        r.lastActivityTs = query.value(7).toLongLong();
        r.status = query.value(8).toString();
        if (query.value(9).toInt() != 0)
            r.status = QStringLiteral("resolved");
        r.kind = query.value(11).toString();
        if (r.kind.isEmpty())
            r.kind = QStringLiteral("group");
        r.participants = participantsFromJson(
            QJsonDocument::fromJson(query.value(12).toString().toUtf8()).array());
        index.insert(r.id, reports.size());
        reports << r;
    }

    QSqlQuery messages(m_db);
    if (messages.exec(QStringLiteral(
            "SELECT id, report_id, sender_id, sender_name, text, ts, timestamp, is_read"
            " FROM messages ORDER BY timestamp, id"))) {
        while (messages.next()) {
            const auto it = index.constFind(messages.value(1).toString());
            if (it == index.cend())
                continue;
            Message m;
            m.id = messages.value(0).toString();
            m.senderId = messages.value(2).toString();
            m.senderName = messages.value(3).toString();
            m.text = messages.value(4).toString();
            m.ts = messages.value(5).toString();
            m.timestamp = messages.value(6).toLongLong();
            m.isRead = messages.value(7).toInt() != 0;
            reports[it.value()].messages << m;
        }
    }

    QSqlQuery attachments(m_db);
    if (attachments.exec(QStringLiteral(
            "SELECT report_id, message_id, filename, display_name, size, owner_id, owner_name, downloaded"
            " FROM attachments ORDER BY rowid"))) {
        while (attachments.next()) {
            const QString reportId = attachments.value(0).toString();
            const auto it = index.constFind(reportId);
            if (it == index.cend())
                continue;

            Attachment a;
            a.messageId = attachments.value(1).toString();
            a.filename = attachments.value(2).toString();
            a.displayName = attachments.value(3).toString();
            if (a.displayName.isEmpty())
                a.displayName = a.filename;
            a.size = attachments.value(4).toLongLong();
            a.ownerId = attachments.value(5).toString();
            a.ownerName = attachments.value(6).toString();
            a.downloaded = attachments.value(7).toInt() != 0;
            if (!a.filename.isEmpty())
                a.downloaded = QFileInfo::exists(attachmentsDir() + QLatin1Char('/') + a.filename);

            if (a.messageId.isEmpty()) {
                reports[it.value()].attachments << a;
            } else {
                for (Message& m : reports[it.value()].messages) {
                    if (m.id == a.messageId) {
                        m.attachments << a;
                        break;
                    }
                }
            }
        }
    }

    return reports;
}

Report ReportManager::loadOne(const QString& id) const
{
    Report report;
    if (!m_db.isOpen() || id.isEmpty())
        return report;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, title, description, author, author_id, created, created_ts,"
        "       last_activity_ts, status, archived, kind, participants"
        "  FROM reports WHERE id = ? AND deleted = 0"));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return report;

    report.id = query.value(0).toString();
    report.title = query.value(1).toString();
    report.description = query.value(2).toString();
    report.author = query.value(3).toString();
    report.authorId = query.value(4).toString();
    report.created = query.value(5).toString();
    report.createdTs = query.value(6).toLongLong();
    report.lastActivityTs = query.value(7).toLongLong();
    report.status = query.value(8).toString();
    if (query.value(9).toInt() != 0)
        report.status = QStringLiteral("resolved");
    report.kind = query.value(10).toString();
    if (report.kind.isEmpty())
        report.kind = QStringLiteral("group");
    report.participants = participantsFromJson(
        QJsonDocument::fromJson(query.value(11).toString().toUtf8()).array());

    QSqlQuery messages(m_db);
    messages.prepare(QStringLiteral(
        "SELECT id, sender_id, sender_name, text, ts, timestamp, is_read"
        "  FROM messages WHERE report_id = ? ORDER BY timestamp, id"));
    messages.addBindValue(id);
    if (messages.exec()) {
        while (messages.next()) {
            Message m;
            m.id = messages.value(0).toString();
            m.senderId = messages.value(1).toString();
            m.senderName = messages.value(2).toString();
            m.text = messages.value(3).toString();
            m.ts = messages.value(4).toString();
            m.timestamp = messages.value(5).toLongLong();
            m.isRead = messages.value(6).toInt() != 0;
            report.messages << m;
        }
    }

    QSqlQuery attachments(m_db);
    attachments.prepare(QStringLiteral(
        "SELECT message_id, filename, display_name, size, owner_id, owner_name, downloaded"
        "  FROM attachments WHERE report_id = ? ORDER BY rowid"));
    attachments.addBindValue(id);
    if (attachments.exec()) {
        while (attachments.next()) {
            Attachment a;
            a.messageId = attachments.value(0).toString();
            a.filename = attachments.value(1).toString();
            a.displayName = attachments.value(2).toString();
            if (a.displayName.isEmpty())
                a.displayName = a.filename;
            a.size = attachments.value(3).toLongLong();
            a.ownerId = attachments.value(4).toString();
            a.ownerName = attachments.value(5).toString();
            a.downloaded = attachments.value(6).toInt() != 0;
            if (!a.filename.isEmpty())
                a.downloaded = QFileInfo::exists(attachmentsDir() + QLatin1Char('/') + a.filename);

            if (a.messageId.isEmpty()) {
                report.attachments << a;
            } else {
                for (Message& m : report.messages) {
                    if (m.id == a.messageId) {
                        m.attachments << a;
                        break;
                    }
                }
            }
        }
    }

    return report;
}

// =====================================================================
//  Mutations
// =====================================================================
bool ReportManager::archiveConversation(const QString& id)
{
    if (!m_db.isOpen() || id.isEmpty())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE reports SET archived = 1, status = 'resolved' WHERE id = ? AND deleted = 0"));
    q.addBindValue(id);
    if (!q.exec()) {
        setError(q, QStringLiteral("Не удалось перенести чат в архив"));
        return false;
    }
    bumpRevision();
    return q.numRowsAffected() > 0;
}

bool ReportManager::deleteConversation(const QString& id)
{
    if (!m_db.isOpen() || id.isEmpty())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE reports SET deleted = 1 WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        setError(q, QStringLiteral("Не удалось удалить чат"));
        return false;
    }
    bumpRevision();
    return q.numRowsAffected() > 0;
}

bool ReportManager::markConversationRead(const QString& id)
{
    if (!m_db.isOpen() || id.isEmpty())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE messages SET is_read = 1"
        " WHERE report_id = ? AND is_read = 0 AND sender_id <> ?"));
    q.addBindValue(id);
    q.addBindValue(selfId());
    if (!q.exec()) {
        setError(q, QStringLiteral("Не удалось обновить статус прочтения"));
        return false;
    }
    if (q.numRowsAffected() > 0)
        bumpRevision();
    return true;
}

int ReportManager::unreadIncomingCount() const
{
    if (!m_db.isOpen())
        return 0;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM messages m"
        " JOIN reports r ON r.id = m.report_id"
        " WHERE m.is_read = 0 AND m.sender_id <> ?"
        "   AND r.deleted = 0 AND r.archived = 0"));
    q.addBindValue(selfId());
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

QString ReportManager::getAttachmentPath(const QString& filename) const
{
    return attachmentsDir() + QLatin1Char('/') + sanitizeFileName(filename);
}

bool ReportManager::setAttachmentDownloaded(const QString& reportId, const QString& filename,
                                            bool downloaded)
{
    if (!m_db.isOpen() || reportId.isEmpty() || filename.isEmpty())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE attachments SET downloaded = ?"
        " WHERE report_id = ? AND filename = ?"));
    q.addBindValue(downloaded ? 1 : 0);
    q.addBindValue(reportId);
    q.addBindValue(sanitizeFileName(filename));
    return q.exec();
}

bool ReportManager::markAttachmentDownloaded(const QString& filename)
{
    if (!m_db.isOpen() || filename.isEmpty())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE attachments SET downloaded = 1 WHERE filename = ?"));
    q.addBindValue(sanitizeFileName(filename));
    return q.exec();
}

Attachment ReportManager::attachmentMeta(const QString& reportId, const QString& filename) const
{
    Attachment a;
    a.filename = sanitizeFileName(filename);
    if (!m_db.isOpen() || reportId.isEmpty() || a.filename.isEmpty())
        return a;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT message_id, display_name, size, owner_id, owner_name, downloaded"
        "  FROM attachments WHERE report_id = ? AND filename = ? LIMIT 1"));
    q.addBindValue(reportId);
    q.addBindValue(a.filename);
    if (q.exec() && q.next()) {
        a.messageId = q.value(0).toString();
        a.displayName = q.value(1).toString();
        if (a.displayName.isEmpty())
            a.displayName = a.filename;
        a.size = q.value(2).toLongLong();
        a.ownerId = q.value(3).toString();
        a.ownerName = q.value(4).toString();
        a.downloaded = q.value(5).toInt() != 0;
        if (!a.filename.isEmpty())
            a.downloaded = QFileInfo::exists(attachmentsDir() + QLatin1Char('/') + a.filename);
    }
    return a;
}

QSet<QString> ReportManager::participantIds(const QString& id) const
{
    QSet<QString> result;
    const Report report = loadOne(id);
    for (const Participant& p : report.participants) {
        if (!p.id.isEmpty())
            result.insert(p.id);
    }
    return result;
}

QJsonArray ReportManager::historyPayloadsFor(const QString& requesterId) const
{
    QJsonArray array;
    if (requesterId.isEmpty())
        return array;

    for (const Report& report : loadAll()) {
        if (!report.isParticipant(requesterId))
            continue;
        array.append(conversationToJson(report));
    }
    return array;
}

bool ReportManager::updateParticipantName(const QString& id, const QString& name)
{
    if (!m_db.isOpen() || id.isEmpty() || name.trimmed().isEmpty())
        return false;

    const QString clean = name.trimmed();
    bool changed = false;

    // Имена отправителей в сообщениях.
    QSqlQuery msg(m_db);
    msg.prepare(QStringLiteral(
        "UPDATE messages SET sender_name = ? WHERE sender_id = ? AND sender_name <> ?"));
    msg.addBindValue(clean);
    msg.addBindValue(id);
    msg.addBindValue(clean);
    if (msg.exec() && msg.numRowsAffected() > 0)
        changed = true;

    // Участники чатов.
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, participants FROM reports WHERE participants <> ''")))
        return changed;

    QList<QPair<QString, QString>> updates;
    while (q.next()) {
        const QString reportId = q.value(0).toString();
        QList<Participant> participants = participantsFromJson(
            QJsonDocument::fromJson(q.value(1).toString().toUtf8()).array());
        bool localChanged = false;
        for (Participant& p : participants) {
            if (p.id == id && p.name != clean) {
                p.name = clean;
                localChanged = true;
            }
        }
        if (localChanged) {
            updates.append({reportId, QString::fromUtf8(
                QJsonDocument(participantsToJson(participants)).toJson(QJsonDocument::Compact))});
        }
    }

    if (!updates.isEmpty()) {
        QSqlQuery upd(m_db);
        upd.prepare(QStringLiteral("UPDATE reports SET participants = ? WHERE id = ?"));
        for (const auto& pair : updates) {
            upd.addBindValue(pair.second);
            upd.addBindValue(pair.first);
            upd.exec();
        }
        changed = true;
    }

    if (changed)
        bumpRevision();
    return changed;
}

QJsonObject ReportManager::exportConversation(const QString& id) const
{
    const Report report = loadOne(id);
    if (report.id.isEmpty())
        return {};
    return conversationToJson(report);
}

// =====================================================================
//  Service
// =====================================================================
int ReportManager::totalCount() const
{
    if (!m_db.isOpen())
        return 0;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM reports WHERE deleted = 0 AND archived = 0"))
        && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

int ReportManager::resolvedCount() const
{
    if (!m_db.isOpen())
        return 0;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM reports WHERE deleted = 0 AND (archived = 1 OR status = 'resolved')"))
        && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

bool ReportManager::optimize()
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA optimize"));
    if (!q.exec(QStringLiteral("VACUUM"))) {
        setError(q, QStringLiteral("Не удалось оптимизировать базу"));
        return false;
    }
    q.exec(QStringLiteral("ANALYZE"));
    return true;
}

void ReportManager::setMeta(const QString& key, const QString& value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

QString ReportManager::meta(const QString& key) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

void ReportManager::setError(const QSqlQuery& query, const QString& context) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, query.lastError().text());
}

QJsonObject ReportManager::toJson(const Report& r) const
{
    return conversationToJson(r);
}

Report ReportManager::fromJson(const QJsonObject& o) const
{
    Report r;
    r.id = o.value(QStringLiteral("id")).toString();
    r.title = o.value(QStringLiteral("title")).toString();
    r.description = o.value(QStringLiteral("description")).toString();
    r.author = o.value(QStringLiteral("author")).toString();
    r.authorId = o.value(QStringLiteral("authorId")).toString();
    r.created = o.value(QStringLiteral("created")).toString();
    r.status = o.value(QStringLiteral("status")).toString(QStringLiteral("open"));
    r.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("group"));
    r.createdTs = o.value(QStringLiteral("created_ts")).toVariant().toLongLong();
    if (r.createdTs <= 0)
        r.createdTs = o.value(QStringLiteral("createdTs")).toVariant().toLongLong();
    if (r.createdTs <= 0)
        r.createdTs = parseCreatedTs(r.created);
    r.lastActivityTs = o.value(QStringLiteral("lastActivityTs")).toVariant().toLongLong();
    if (r.lastActivityTs <= 0)
        r.lastActivityTs = r.createdTs;

    r.participants = participantsFromJson(
        o.value(QStringLiteral("participants")).toArray());

    for (const QJsonValue& value : o.value(QStringLiteral("attachments")).toArray()) {
        if (value.isString()) {
            Attachment a;
            a.filename = sanitizeFileName(value.toString());
            a.downloaded = true;
            a.ownerId = r.authorId;
            a.ownerName = r.author;
            r.attachments << a;
        } else if (value.isObject()) {
            r.attachments << attachmentFromJson(value.toObject());
        }
    }

    for (const QJsonValue& value : o.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject mo = value.toObject();
        Message m;
        m.id = mo.value(QStringLiteral("id")).toString();
        if (m.id.isEmpty())
            m.id = makeMessageId();
        m.senderId = mo.value(QStringLiteral("senderId")).toString();
        m.senderName = mo.value(QStringLiteral("senderName")).toString();
        if (m.senderName.isEmpty())
            m.senderName = mo.value(QStringLiteral("author")).toString();
        m.text = mo.value(QStringLiteral("text")).toString();
        m.ts = mo.value(QStringLiteral("ts")).toString();
        m.timestamp = mo.value(QStringLiteral("timestamp")).toVariant().toLongLong();
        if (m.timestamp <= 0)
            m.timestamp = r.lastActivityTs;
        m.isRead = mo.value(QStringLiteral("isRead")).toBool(true);

        const QString role = mo.value(QStringLiteral("role")).toString();
        if (m.senderId.isEmpty() && role == QLatin1String("user"))
            m.senderId = r.authorId;
        if (m.senderName.isEmpty() && !m.senderId.isEmpty())
            m.senderName = m.senderId.left(8);

        for (const QJsonValue& av : mo.value(QStringLiteral("attachments")).toArray()) {
            if (av.isString()) {
                Attachment a;
                a.filename = sanitizeFileName(av.toString());
                a.messageId = m.id;
                a.ownerId = m.senderId;
                a.ownerName = m.senderName;
                a.downloaded = true;
                m.attachments << a;
            } else if (av.isObject()) {
                Attachment a = attachmentFromJson(av.toObject());
                a.messageId = m.id;
                m.attachments << a;
            }
        }

        r.messages << m;
        if (m.timestamp > r.lastActivityTs)
            r.lastActivityTs = m.timestamp;
    }
    return r;
}
