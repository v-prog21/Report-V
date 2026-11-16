#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct DeviceTest {
    QString orgReg;
    QString numDoc;
    QString beginDate;   // ISO yyyy-MM-dd (если удалось распознать)
    QString endDate;
};

struct Device {
    QString id;
    QString name;
    QString regNum;
    QString factoryNum;
    QString facIds;
    QString siErr;
    QString dopFld3;

    int noTest = 0;
    bool active = true;

    QString latestBegin;
    QString latestEnd;
    int testCount = 0;
};

struct DeviceImportResult {
    bool ok = false;
    int devices = 0;
    int persons = 0;
    QString error;
};

/// Локальный кэш приборов (SQLite). Источник данных — MDB (ATT_DEVICE*).
/// База хранится отдельно от отчётов, поэтому доступна даже без MDB-файла.
class DeviceRepository {
public:
    explicit DeviceRepository(QString dbPath);
    ~DeviceRepository();

    DeviceRepository(const DeviceRepository&) = delete;
    DeviceRepository& operator=(const DeviceRepository&) = delete;

    bool isValid() const { return m_valid; }
    QString dbPath() const { return m_dbPath; }
    QString lastError() const { return m_lastError; }

    QList<Device> loadDevices();
    QList<DeviceTest> testsForDevice(const QString& deviceId) const;

    int deviceCount() const;
    int personCount() const;
    QString lastSync() const;
    QString sourcePath() const;

    DeviceImportResult importFromMdb(const QString& mdbPath);
    bool vacuum();

private:
    bool open();
    bool ensureSchema();
    void recomputeActive(const QList<Device>& devices);
    void setMeta(const QString& key, const QString& value);

    QString m_dbPath;
    QString m_connectionName;
    QSqlDatabase m_db;
    bool m_valid = false;
    mutable QString m_lastError;
};
