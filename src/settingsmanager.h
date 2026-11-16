#pragma once

#include <QSettings>
#include <QString>

class SettingsManager {
public:
    static SettingsManager& instance();

    QString reportsPath() const;
    void setReportsPath(const QString& path);

    QString mdbPath() const;
    void setMdbPath(const QString& path);

    /// Локальный кэш приборов (SQLite). По умолчанию — в AppData,
    /// чтобы база не конфликтовала при работе нескольких копий приложения.
    QString devicesDbPath() const;

    QString pcName() const;
    void setPcName(const QString& name);

    /// Постоянный идентификатор этого компьютера в локальной сети.
    QString networkId() const;

    int networkPort() const;
    void setNetworkPort(int port);

    bool showAllReports() const;
    void setShowAllReports(bool value);

private:
    SettingsManager();

    mutable QSettings m_settings;
};
