#include "devicerepository.h"

#include "deviceutils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

QString makeConnectionName(const QString& prefix)
{
    return QStringLiteral("%1_%2").arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString accessConnectionString(const QString& driver, const QString& path)
{
    return QStringLiteral("DRIVER={%1};DBQ=%2;ReadOnly=1;")
        .arg(driver, QDir::toNativeSeparators(path));
}

} // namespace

DeviceRepository::DeviceRepository(QString dbPath)
    : m_dbPath(std::move(dbPath))
{
    open();
}

DeviceRepository::~DeviceRepository()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
    }
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DeviceRepository::open()
{
    if (m_dbPath.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Не задан путь к локальной базе приборов.");
        return false;
    }

    const QString dir = QFileInfo(m_dbPath).absolutePath();
    if (!QDir().mkpath(dir)) {
        m_lastError = QStringLiteral("Не удалось создать папку для базы приборов:\n%1").arg(dir);
        return false;
    }

    m_connectionName = makeConnectionName(QStringLiteral("devices"));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_dbPath);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));

    if (!m_db.open()) {
        m_lastError = QStringLiteral("Не удалось открыть локальную базу приборов:\n%1")
                          .arg(m_db.lastError().text());
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode=DELETE"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    if (!ensureSchema())
        return false;

    m_valid = true;
    return true;
}

bool DeviceRepository::ensureSchema()
{
    QSqlQuery q(m_db);
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS devices ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT,"
            "  reg_num TEXT,"
            "  factory_num TEXT,"
            "  no_test INTEGER DEFAULT 0,"
            "  fac_ids TEXT,"
            "  si_err TEXT,"
            "  dop_fld3 TEXT,"
            "  active INTEGER DEFAULT 1,"
            "  data_hash TEXT"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS device_tests ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  dev_id TEXT NOT NULL,"
            "  org_reg TEXT,"
            "  num_doc TEXT,"
            "  begin_date TEXT,"
            "  end_date TEXT,"
            "  FOREIGN KEY (dev_id) REFERENCES devices(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tests_dev ON device_tests(dev_id, begin_date DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tests_doc ON device_tests(num_doc)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS persons ("
            "  id_person TEXT PRIMARY KEY,"
            "  fio TEXT,"
            "  dolg TEXT"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS meta ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT"
            ")")
    };

    for (const QString& sql : statements) {
        if (!q.exec(sql)) {
            m_lastError = QStringLiteral("Ошибка инициализации базы приборов:\n%1")
                              .arg(q.lastError().text());
            return false;
        }
    }
    return true;
}

QList<Device> DeviceRepository::loadDevices()
{
    QList<Device> devices;
    if (!m_valid)
        return devices;

    QSqlQuery q(m_db);
    // Один запрос вместо трёх коррелированных подзапросов на каждую строку.
    // Последняя поверка берётся из агрегата по максимальной дате.
    const QString sql = QStringLiteral(
        "SELECT d.id, COALESCE(d.name,''), COALESCE(d.reg_num,''), COALESCE(d.factory_num,''),"
        "       COALESCE(d.no_test,0), COALESCE(d.fac_ids,''), COALESCE(d.si_err,''),"
        "       COALESCE(d.dop_fld3,''), COALESCE(d.active,1),"
        "       COALESCE(lt.begin_date,''), COALESCE(lt.end_date,''), COALESCE(tc.cnt,0)"
        "  FROM devices d"
        "  LEFT JOIN ("
        "      SELECT dev_id, MAX(begin_date) AS max_begin, COUNT(*) AS cnt"
        "        FROM device_tests GROUP BY dev_id"
        "  ) tc ON tc.dev_id = d.id"
        "  LEFT JOIN device_tests lt"
        "         ON lt.dev_id = d.id AND lt.begin_date = tc.max_begin"
        "            AND lt.id = (SELECT MAX(id) FROM device_tests t2"
        "                          WHERE t2.dev_id = d.id AND t2.begin_date = tc.max_begin)"
        " ORDER BY d.name COLLATE NOCASE");

    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        return devices;
    }

    devices.reserve(512);
    const QDate today = QDate::currentDate();

    while (q.next()) {
        Device d;
        d.id          = q.value(0).toString();
        d.name        = q.value(1).toString();
        d.regNum      = q.value(2).toString();
        d.factoryNum  = q.value(3).toString();
        d.noTest      = q.value(4).toInt();
        d.facIds      = q.value(5).toString();
        d.siErr       = q.value(6).toString();
        d.dopFld3     = q.value(7).toString();
        d.active      = q.value(8).toInt() != 0;
        d.latestBegin = DeviceUtils::normalizeDate(q.value(9));
        d.latestEnd   = DeviceUtils::normalizeDate(q.value(10));
        d.testCount   = q.value(11).toInt();

        // Пересчитываем активность в памяти, чтобы UI сразу видел актуальный
        // статус без лишних записей в базу при каждом открытии вкладки.
        const QDate end = DeviceUtils::parseDate(d.latestEnd);
        if (end.isValid())
            d.active = (end >= today);
        devices << d;
    }

    return devices;
}

void DeviceRepository::recomputeActive(const QList<Device>& devices)
{
    if (devices.isEmpty())
        return;

    QSqlQuery update(m_db);
    update.prepare(QStringLiteral("UPDATE devices SET active = ? WHERE id = ? AND active <> ?"));

    const QDate today = QDate::currentDate();
    bool inTransaction = m_db.transaction();

    for (const Device& d : devices) {
        const QDate end = DeviceUtils::parseDate(d.latestEnd);
        const bool active = !(end.isValid() && end < today);
        if (active == d.active)
            continue;

        update.bindValue(0, active ? 1 : 0);
        update.bindValue(1, d.id);
        update.bindValue(2, d.active ? 1 : 0);
        update.exec();
    }

    if (inTransaction)
        m_db.commit();
}

QList<DeviceTest> DeviceRepository::testsForDevice(const QString& deviceId) const
{
    QList<DeviceTest> tests;
    if (!m_valid || deviceId.isEmpty())
        return tests;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(org_reg,''), COALESCE(num_doc,''),"
        "       COALESCE(begin_date,''), COALESCE(end_date,'')"
        "  FROM device_tests WHERE dev_id = ?"
        " ORDER BY begin_date DESC, id DESC"));
    q.addBindValue(deviceId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return tests;
    }

    while (q.next()) {
        DeviceTest t;
        t.orgReg    = q.value(0).toString();
        t.numDoc    = q.value(1).toString();
        t.beginDate = DeviceUtils::normalizeDate(q.value(2));
        t.endDate   = DeviceUtils::normalizeDate(q.value(3));
        tests << t;
    }
    return tests;
}

int DeviceRepository::deviceCount() const
{
    if (!m_valid)
        return 0;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM devices")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int DeviceRepository::personCount() const
{
    if (!m_valid)
        return 0;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM persons")) && q.next())
        return q.value(0).toInt();
    return 0;
}

QString DeviceRepository::lastSync() const
{
    if (!m_valid)
        return {};
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'last_sync'")) && q.next())
        return q.value(0).toString();
    return {};
}

QString DeviceRepository::sourcePath() const
{
    if (!m_valid)
        return {};
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'source_path'")) && q.next())
        return q.value(0).toString();
    return {};
}

void DeviceRepository::setMeta(const QString& key, const QString& value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES (?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

DeviceImportResult DeviceRepository::importFromMdb(const QString& mdbPath)
{
    DeviceImportResult result;

    if (!m_valid) {
        result.error = m_lastError.isEmpty()
                           ? QStringLiteral("Локальная база приборов недоступна.")
                           : m_lastError;
        return result;
    }

    const QFileInfo fileInfo(mdbPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.error = QStringLiteral("Файл MDB не найден:\n%1").arg(mdbPath);
        return result;
    }

    QList<Device> devices;
    QHash<QString, QList<DeviceTest>> testsByDevice;
    QHash<QString, QString> latestEndByDevice;

    struct PersonRow { QString id; QString fio; QString dolg; };
    QList<PersonRow> persons;

    const QString mdbConnectionName = makeConnectionName(QStringLiteral("mdb_import"));
    QString accessError;

    {
        QSqlDatabase mdb = QSqlDatabase::addDatabase(QStringLiteral("QODBC"), mdbConnectionName);
        mdb.setDatabaseName(accessConnectionString(
            QStringLiteral("Microsoft Access Driver (*.mdb, *.accdb)"), fileInfo.absoluteFilePath()));

        if (!mdb.open()) {
            accessError = mdb.lastError().text();
            mdb.setDatabaseName(accessConnectionString(
                QStringLiteral("Microsoft Access Driver (*.mdb)"), fileInfo.absoluteFilePath()));
            if (!mdb.open()) {
                accessError += QStringLiteral("\n") + mdb.lastError().text();
            }
        }

        if (!mdb.isOpen()) {
            result.error = QStringLiteral(
                "Не удалось подключиться к базе MDB.\n%1\n\n"
                "Проверьте, что установлен драйвер Microsoft Access Database Engine "
                "той же разрядности, что и приложение.")
                .arg(accessError.trimmed());
            mdb = QSqlDatabase();
            QSqlDatabase::removeDatabase(mdbConnectionName);
            return result;
        }

        // --- Приборы -----------------------------------------------------
        {
            QSqlQuery q(mdb);
            q.setForwardOnly(true);
            if (!q.exec(QStringLiteral(
                    "SELECT id, name, reg_num, factory_num, no_test, fac_ids, si_err, dop_fld3"
                    " FROM ATT_DEVICE"))) {
                result.error = QStringLiteral("Не удалось прочитать таблицу ATT_DEVICE:\n%1")
                                   .arg(q.lastError().text());
                mdb.close();
                mdb = QSqlDatabase();
                QSqlDatabase::removeDatabase(mdbConnectionName);
                return result;
            }

            devices.reserve(512);
            while (q.next()) {
                Device d;
                d.id         = q.value(0).toString().trimmed();
                if (d.id.isEmpty())
                    continue;
                d.name       = q.value(1).toString().trimmed();
                d.regNum     = q.value(2).toString().trimmed();
                d.factoryNum = q.value(3).toString().trimmed();
                d.noTest     = q.value(4).toInt();
                d.facIds     = q.value(5).toString().trimmed();
                d.siErr      = q.value(6).toString().trimmed();
                d.dopFld3    = q.value(7).toString().trimmed();
                devices << d;
            }
        }

        // --- Поверки -----------------------------------------------------
        {
            QSqlQuery q(mdb);
            q.setForwardOnly(true);
            if (!q.exec(QStringLiteral(
                    "SELECT dev_id, org_reg, num_doc, begin_date, end_date"
                    " FROM ATT_DEVICE_TEST ORDER BY begin_date"))) {
                result.error = QStringLiteral("Не удалось прочитать таблицу ATT_DEVICE_TEST:\n%1")
                                   .arg(q.lastError().text());
                mdb.close();
                mdb = QSqlDatabase();
                QSqlDatabase::removeDatabase(mdbConnectionName);
                return result;
            }

            while (q.next()) {
                const QString devId = q.value(0).toString().trimmed();
                if (devId.isEmpty())
                    continue;

                DeviceTest t;
                t.orgReg    = q.value(1).toString().trimmed();
                t.numDoc    = q.value(2).toString().trimmed();
                t.beginDate = DeviceUtils::normalizeDate(q.value(3));
                t.endDate   = DeviceUtils::normalizeDate(q.value(4));
                testsByDevice[devId] << t;

                const QDate end = DeviceUtils::parseDate(t.endDate);
                const QString current = latestEndByDevice.value(devId);
                const QDate currentDate = DeviceUtils::parseDate(current);
                if (end.isValid() && (!currentDate.isValid() || end > currentDate))
                    latestEndByDevice[devId] = end.toString(Qt::ISODate);
            }
        }

        // --- Персонал ----------------------------------------------------
        {
            QSqlQuery q(mdb);
            q.setForwardOnly(true);
            if (q.exec(QStringLiteral("SELECT id, fio, dolg FROM ATT_PERSON"))) {
                while (q.next()) {
                    PersonRow p;
                    p.id   = q.value(0).toString().trimmed();
                    p.fio  = q.value(1).toString().trimmed();
                    p.dolg = q.value(2).toString().trimmed();
                    if (!p.id.isEmpty())
                        persons << p;
                }
            }
            // Отсутствие таблицы персонала не считается критичной ошибкой.
        }

        mdb.close();
    }
    QSqlDatabase::removeDatabase(mdbConnectionName);

    // --- Сохранение в локальную SQLite ----------------------------------
    if (!m_db.transaction()) {
        result.error = QStringLiteral("Не удалось начать транзакцию:\n%1")
                           .arg(m_db.lastError().text());
        return result;
    }

    auto rollback = [this](const QString& message) {
        m_db.rollback();
        DeviceImportResult r;
        r.error = message;
        return r;
    };

    QSqlQuery q(m_db);
    for (const QString& sql : {
             QStringLiteral("DELETE FROM device_tests"),
             QStringLiteral("DELETE FROM devices"),
             QStringLiteral("DELETE FROM persons")}) {
        if (!q.exec(sql))
            return rollback(QStringLiteral("Ошибка очистки локальной базы:\n%1")
                                .arg(q.lastError().text()));
    }

    QSqlQuery insDevice(m_db);
    insDevice.prepare(QStringLiteral(
        "INSERT INTO devices"
        " (id, name, reg_num, factory_num, no_test, fac_ids, si_err, dop_fld3, active, data_hash)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    QSqlQuery insTest(m_db);
    insTest.prepare(QStringLiteral(
        "INSERT INTO device_tests (dev_id, org_reg, num_doc, begin_date, end_date)"
        " VALUES (?, ?, ?, ?, ?)"));

    const QDate today = QDate::currentDate();

    for (const Device& d : devices) {
        const QString endRaw = latestEndByDevice.value(d.id);
        const QDate end = DeviceUtils::parseDate(endRaw);
        const bool active = !(end.isValid() && end < today);

        const QByteArray hashInput = (d.id + d.name + d.factoryNum + d.facIds + d.siErr).toUtf8();
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(hashInput, QCryptographicHash::Md5).toHex());

        insDevice.bindValue(0, d.id);
        insDevice.bindValue(1, d.name);
        insDevice.bindValue(2, d.regNum);
        insDevice.bindValue(3, d.factoryNum);
        insDevice.bindValue(4, d.noTest);
        insDevice.bindValue(5, d.facIds);
        insDevice.bindValue(6, d.siErr);
        insDevice.bindValue(7, d.dopFld3);
        insDevice.bindValue(8, active ? 1 : 0);
        insDevice.bindValue(9, hash);
        if (!insDevice.exec())
            return rollback(QStringLiteral("Ошибка записи прибора «%1»:\n%2")
                                .arg(d.name, insDevice.lastError().text()));

        for (const DeviceTest& t : testsByDevice.value(d.id)) {
            insTest.bindValue(0, d.id);
            insTest.bindValue(1, t.orgReg);
            insTest.bindValue(2, t.numDoc);
            insTest.bindValue(3, t.beginDate);
            insTest.bindValue(4, t.endDate);
            if (!insTest.exec())
                return rollback(QStringLiteral("Ошибка записи поверки:\n%1")
                                    .arg(insTest.lastError().text()));
        }
    }

    QSqlQuery insPerson(m_db);
    insPerson.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO persons (id_person, fio, dolg) VALUES (?, ?, ?)"));
    for (const PersonRow& p : persons) {
        insPerson.bindValue(0, p.id);
        insPerson.bindValue(1, p.fio);
        insPerson.bindValue(2, p.dolg);
        if (!insPerson.exec())
            return rollback(QStringLiteral("Ошибка записи персонала:\n%1")
                                .arg(insPerson.lastError().text()));
    }

    setMeta(QStringLiteral("last_sync"),
            QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm")));
    setMeta(QStringLiteral("source_path"), fileInfo.absoluteFilePath());
    setMeta(QStringLiteral("source_mtime"),
            QString::number(fileInfo.lastModified().toSecsSinceEpoch()));
    setMeta(QStringLiteral("device_count"), QString::number(devices.size()));
    setMeta(QStringLiteral("person_count"), QString::number(persons.size()));

    if (!m_db.commit())
        return rollback(QStringLiteral("Не удалось сохранить данные:\n%1")
                            .arg(m_db.lastError().text()));

    m_lastError.clear();
    result.ok = true;
    result.devices = devices.size();
    result.persons = persons.size();
    return result;
}

bool DeviceRepository::vacuum()
{
    if (!m_valid)
        return false;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("VACUUM"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("ANALYZE"));
    return true;
}
