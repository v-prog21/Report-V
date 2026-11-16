#include "settingsmanager.h"

#include <QDir>
#include <QHostInfo>
#include <QStandardPaths>
#include <QUuid>

SettingsManager& SettingsManager::instance()
{
    static SettingsManager manager;
    return manager;
}

SettingsManager::SettingsManager()
    : m_settings(QStringLiteral("BugTracker"), QStringLiteral("BugTrackerApp"))
{
    // Настройки прежней версии приложения (SupportIL) не должны теряться:
    // переносим из них путь к MDB, если в основной ветке он ещё не задан.
    QSettings legacy(QStringLiteral("SupportIL"), QStringLiteral("SupportILApp"));
    if (!m_settings.contains(QStringLiteral("paths/mdb"))
        && legacy.contains(QStringLiteral("paths/mdb"))) {
        m_settings.setValue(QStringLiteral("paths/mdb"),
                            legacy.value(QStringLiteral("paths/mdb")));
    }
}

QString SettingsManager::reportsPath() const
{
    // Удобно для автотестов и переносимых запусков: путь можно задать
    // переменной окружения, не меняя настройки пользователя.
    const QString overridePath = qEnvironmentVariable("SUPPORTIL_REPORTS_PATH");
    if (!overridePath.trimmed().isEmpty())
        return overridePath;

    return m_settings.value(QStringLiteral("paths/reports"),
                            QDir::homePath() + QStringLiteral("/BugTracker"))
        .toString();
}

void SettingsManager::setReportsPath(const QString& path)
{
    m_settings.setValue(QStringLiteral("paths/reports"), path);
}

QString SettingsManager::mdbPath() const
{
    return m_settings.value(QStringLiteral("paths/mdb")).toString();
}

void SettingsManager::setMdbPath(const QString& path)
{
    m_settings.setValue(QStringLiteral("paths/mdb"), path);
}

QString SettingsManager::devicesDbPath() const
{
    const QString configured = m_settings.value(QStringLiteral("paths/devicesDb")).toString();
    if (!configured.isEmpty())
        return configured;

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/SupportIL");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/devices.db");
}

QString SettingsManager::pcName() const
{
    QString name = m_settings.value(QStringLiteral("pc/name")).toString();
    if (name.isEmpty()) {
        name = QHostInfo::localHostName();
        m_settings.setValue(QStringLiteral("pc/name"), name);
    }
    return name;
}

void SettingsManager::setPcName(const QString& name)
{
    m_settings.setValue(QStringLiteral("pc/name"), name);
}

QString SettingsManager::networkId() const
{
    QString id = m_settings.value(QStringLiteral("network/id")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.setValue(QStringLiteral("network/id"), id);
    }
    return id;
}

int SettingsManager::networkPort() const
{
    return m_settings.value(QStringLiteral("network/port"), 39417).toInt();
}

void SettingsManager::setNetworkPort(int port)
{
    m_settings.setValue(QStringLiteral("network/port"), port);
}

bool SettingsManager::showAllReports() const
{
    return m_settings.value(QStringLiteral("view/showAll"), false).toBool();
}

void SettingsManager::setShowAllReports(bool value)
{
    m_settings.setValue(QStringLiteral("view/showAll"), value);
}
