#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>

class QSqlQuery;

struct Participant {
    QString id;
    QString name;
};

struct Attachment {
    QString filename;      // имя файла в папке вложений (уникальное)
    QString displayName;   // исходное имя для показа пользователю
    qint64 size = 0;
    QString ownerId;
    QString ownerName;
    bool downloaded = true;
    QString messageId;

    QString visibleName() const
    {
        return displayName.trimmed().isEmpty() ? filename : displayName;
    }
};

struct Message {
    QString id;
    QString senderId;
    QString senderName;
    QString text;
    QString ts;           // отображаемое время, HH:mm
    qint64 timestamp = 0; // мс для порядка и синхронизации
    bool isRead = true;
    QList<Attachment> attachments;
};

struct Report {
    QString id;
    QString title;
    QString description;
    QString author;
    QString authorId;
    QString created;      // dd.MM.yyyy HH:mm
    qint64 createdTs = 0;
    qint64 lastActivityTs = 0;
    QString status;       // "open" | "resolved"
    QString kind;         // "direct" | "group"
    QList<Participant> participants;
    QList<Attachment> attachments; // вложения, не привязанные к сообщению
    QList<Message> messages;

    bool isParticipant(const QString& selfId) const;
    QString displayTitle(const QString& selfId) const;
    QString participantSummary(const QString& selfId) const;
    int unreadIncomingCount(const QString& selfId) const;
};

/// Хранилище чатов. SQLite-база лежит в папке данных приложения.
class ReportManager {
public:
    explicit ReportManager(const QString& basePath = QString());
    ~ReportManager();

    ReportManager(const ReportManager&) = delete;
    ReportManager& operator=(const ReportManager&) = delete;

    void setBasePath(const QString& path);
    QString getBasePath() const { return m_base; }
    QString dbPath() const;
    bool isReady() const { return m_db.isOpen(); }
    QString lastError() const { return m_lastError; }

    /// Идентификатор и имя текущего пользователя. По умолчанию берутся из
    /// настроек; переопределение используется автотестами.
    void setIdentity(const QString& id, const QString& name);
    QString selfId() const { return m_selfId; }
    QString selfName() const { return m_selfName; }

    /// Личный чат с участником. Идентификатор детерминирован, поэтому
    /// обе стороны создают один и тот же чат.
    QString ensureDirectChat(const QString& peerId, const QString& peerName);

    /// Групповой чат. participants должен включать текущего пользователя.
    QString createGroupChat(const QString& title, const QList<Participant>& participants);

    /// Добавляет участников в существующий групповой чат. Возвращает
    /// обновлённое представление чата для рассылки всем участникам.
    bool addParticipants(const QString& id, const QList<Participant>& people,
                         QJsonObject& conversation);

    /// Добавляет исходящее сообщение, копирует выбранные файлы и возвращает
    /// готовый network-payload для рассылки участникам чата.
    bool sendLocalMessage(const QString& id, const QString& text,
                          const QList<QString>& files, QJsonObject& payload,
                          Message& outMessage);

    /// Принимает один payload из локальной сети.
    bool importNetworkMessage(const QJsonObject& payload, bool& isNew);
    /// Принимает событие создания/обновления чата.
    bool importConversation(const QJsonObject& conversation, bool& isNew);

    QList<Report> loadAll() const;
    Report loadOne(const QString& id) const;

    bool archiveConversation(const QString& id);
    bool deleteConversation(const QString& id);
    bool markConversationRead(const QString& id);
    int unreadIncomingCount() const;

    QString getAttachmentPath(const QString& filename) const;
    bool setAttachmentDownloaded(const QString& reportId, const QString& filename,
                                 bool downloaded);
    bool markAttachmentDownloaded(const QString& filename);
    Attachment attachmentMeta(const QString& reportId, const QString& filename) const;

    /// Участники чата (для адресной сетевой доставки).
    QSet<QString> participantIds(const QString& id) const;

    /// История для синхронизации: только чаты, где requester — участник.
    QJsonArray historyPayloadsFor(const QString& requesterId) const;

    /// Обновляет отображаемые имена участника во всех чатах и сообщениях.
    bool updateParticipantName(const QString& id, const QString& name);

    /// Экспорт чата в JSON (для сетевой рассылки).
    QJsonObject exportConversation(const QString& id) const;

    qint64 revision() const { return m_revision; }
    int totalCount() const;
    int resolvedCount() const;
    bool optimize();

private:
    bool openDatabase();
    void closeDatabase();
    bool ensureSchema();
    bool ensureColumn(const QString& table, const QString& column,
                      const QString& definition);
    void migrateLegacyJson(); // deprecated: только импорт старых JSON-отчётов

    QString reportsDir() const;
    QString attachmentsDir() const;
    void setMeta(const QString& key, const QString& value);
    QString meta(const QString& key) const;
    void setError(const QSqlQuery& query, const QString& context) const;
    void bumpRevision() { ++m_revision; }

    // deprecated: совместимость со старой JSON-схемой баг-репортов,
    // используется только migrateLegacyJson().
    QJsonObject toJson(const Report& r) const;
    Report fromJson(const QJsonObject& o) const;

public:
    static QString directChatId(const QString& a, const QString& b);
    static QJsonObject conversationToJson(const Report& r);
    static QJsonObject messageToJson(const Report& r, const Message& m);
    static QJsonObject attachmentToJson(const Attachment& a);
    static Attachment attachmentFromJson(const QJsonObject& o);
    static QList<Participant> participantsFromJson(const QJsonArray& array);
    static QJsonArray participantsToJson(const QList<Participant>& participants);

private:
    QString m_base;
    QString m_connectionName;
    QSqlDatabase m_db;
    mutable QString m_lastError;
    qint64 m_revision = 1;
    QString m_selfId;
    QString m_selfName;
};
