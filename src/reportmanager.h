#pragma once
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

struct Message {
    QString author;
    QString role;      // "user" | "admin"
    QString text;
    QString ts;
    bool isRead = false; // Для мессенджера
};

struct Report {
    QString id;
    QString title;
    QString description;
    QString author;
    QString created;
    QString status;    // "open" | "resolved"
    QStringList attachments;
    QList<Message> messages;
};

class ReportManager {
public:
    explicit ReportManager(const QString& basePath = "");
    void setBasePath(const QString& p);
    QString getBasePath() const { return m_base; }

    QString createReport(const QString& title, const QString& desc, const QList<QString>& tempImages);
    bool addMessage(const QString& id, const QString& text, const QString& role);
    bool resolve(const QString& id);

    QList<Report> loadAll() const;
    QList<Report> loadByAuthor(const QString& author, bool includeResolved) const;
    QList<Report> loadArchive() const;
    Report loadOne(const QString& id) const;
    
    // Добавлено для получения полного пути к картинке
    QString getAttachmentPath(const QString& filename) const;

private:
    QString reportsDir() const;
    QString attachmentsDir() const;
    QString reportFile(const QString& id) const;
    void writeAtomic(const QString& path, const QJsonObject& obj);
    Report fromJson(const QJsonObject& o) const;
    QJsonObject toJson(const Report& r) const;

    QString m_base;
};