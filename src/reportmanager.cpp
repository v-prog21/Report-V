#include "reportmanager.h"
#include "settingsmanager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QFileInfo>

ReportManager::ReportManager(const QString& basePath) : m_base(basePath) {}
void ReportManager::setBasePath(const QString& p) { m_base = p; }

QString ReportManager::reportsDir() const     { return m_base + "/reports"; }
QString ReportManager::attachmentsDir() const { return m_base + "/attachments"; }
QString ReportManager::reportFile(const QString& id) const { return reportsDir() + "/" + id + ".json"; }

QString ReportManager::getAttachmentPath(const QString& filename) const {
    return attachmentsDir() + "/" + filename;
}

void ReportManager::writeAtomic(const QString& path, const QJsonObject& obj) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString tmp = path + ".tmp";
    QFile f(tmp);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        f.close();
        QFile::remove(path);
        QFile::rename(tmp, path);
    }
}

QString ReportManager::createReport(const QString& title, const QString& desc, const QList<QString>& tempImages) {
    QString id = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(4);
    QDir().mkpath(attachmentsDir());
    QDir().mkpath(reportsDir());

    QStringList att;
    for (int i = 0; i < tempImages.size(); ++i) {
        QString name = QString("%1_%2.png").arg(id).arg(i);
        QString dest = attachmentsDir() + "/" + name;
        QFile::copy(tempImages[i], dest);
        att << name;
    }

    Report r;
    r.id = id;
    r.title = title;
    r.description = desc;
    r.author = SettingsManager::instance().pcName();
    r.created = QDateTime::currentDateTime().toString("dd.MM.yyyy HH:mm");
    r.status = "open";
    r.attachments = att;

    writeAtomic(reportFile(id), toJson(r));
    return id;
}

bool ReportManager::addMessage(const QString& id, const QString& text, const QString& role) {
    Report r = loadOne(id);
    if (r.id.isEmpty()) return false;
    
    Message m;
    m.author = SettingsManager::instance().pcName();
    m.role = role;
    m.text = text;
    m.ts = QDateTime::currentDateTime().toString("HH:mm");
    m.isRead = (role == "admin"); // Если пишет админ, сообщение непрочитано пользователем
    r.messages.append(m);
    
    writeAtomic(reportFile(id), toJson(r));
    return true;
}

bool ReportManager::resolve(const QString& id) {
    Report r = loadOne(id);
    if (r.id.isEmpty()) return false;
    r.status = "resolved";
    writeAtomic(reportFile(id), toJson(r));
    return true;
}

QList<Report> ReportManager::loadAll() const {
    QList<Report> out;
    QDir d(reportsDir());
    if (!d.exists()) return out;
    for (const QFileInfo& fi : d.entryInfoList({"*.json"}, QDir::Files, QDir::Time)) {
        QFile f(fi.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly)) {
            out << fromJson(QJsonDocument::fromJson(f.readAll()).object());
        }
    }
    return out;
}

QList<Report> ReportManager::loadByAuthor(const QString& author, bool includeResolved) const {
    QList<Report> out;
    for (const Report& r : loadAll()) {
        if (r.author != author) continue;
        if (!includeResolved && r.status == "resolved") continue;
        out << r;
    }
    return out;
}

QList<Report> ReportManager::loadArchive() const {
    QList<Report> out;
    for (const Report& r : loadAll()) if (r.status == "resolved") out << r;
    return out;
}

Report ReportManager::loadOne(const QString& id) const {
    QFile f(reportFile(id));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

Report ReportManager::fromJson(const QJsonObject& o) const {
    Report r;
    r.id = o["id"].toString();
    r.title = o["title"].toString();
    r.description = o["description"].toString();
    r.author = o["author"].toString();
    r.created = o["created"].toString();
    r.status = o["status"].toString();
    for (auto v : o["attachments"].toArray()) r.attachments << v.toString();
    for (auto v : o["messages"].toArray()) {
        auto mo = v.toObject();
        Message m;
        m.author = mo["author"].toString();
        m.role = mo["role"].toString();
        m.text = mo["text"].toString();
        m.ts = mo["ts"].toString();
        m.isRead = mo["isRead"].toBool(true);
        r.messages << m;
    }
    return r;
}

QJsonObject ReportManager::toJson(const Report& r) const {
    QJsonObject o;
    o["id"] = r.id; o["title"] = r.title; o["description"] = r.description;
    o["author"] = r.author; o["created"] = r.created; o["status"] = r.status;
    QJsonArray att;
    for (const auto& a : r.attachments) att.append(a);
    o["attachments"] = att;
    QJsonArray msgs;
    for (const auto& m : r.messages) {
        QJsonObject mo;
        mo["author"] = m.author; mo["role"] = m.role; mo["text"] = m.text;
        mo["ts"] = m.ts; mo["isRead"] = m.isRead;
        msgs.append(mo);
    }
    o["messages"] = msgs;
    return o;
}