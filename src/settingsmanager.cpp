#include "settingsmanager.h"
#include <QtNetwork/QHostInfo> 
#include <QHostInfo>
#include <QDir>  

SettingsManager& SettingsManager::instance() {
    static SettingsManager s;
    return s;
}

SettingsManager::SettingsManager()
    : m_s("BugTracker", "BugTrackerApp") {}

QString SettingsManager::reportsPath() const {
    return m_s.value("paths/reports", QDir::homePath() + "/BugTracker").toString();
}
void SettingsManager::setReportsPath(const QString& p) {
    m_s.setValue("paths/reports", p);
}

QString SettingsManager::pcName() {
    QString n = m_s.value("pc/name").toString();
    if (n.isEmpty()) {
        n = QHostInfo::localHostName();
        m_s.setValue("pc/name", n);
    }
    return n;
}
void SettingsManager::setPcName(const QString& n) { m_s.setValue("pc/name", n); }

bool SettingsManager::showAllReports() const {
    return m_s.value("view/showAll", false).toBool();
}
void SettingsManager::setShowAllReports(bool v) {
    m_s.setValue("view/showAll", v);
}

QString SettingsManager::adminPassword() const {
    return m_s.value("auth/adminPwd", "admin").toString();
}