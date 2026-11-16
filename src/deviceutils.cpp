#include "deviceutils.h"

#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <climits>

namespace DeviceUtils {

namespace {

const QHash<int, QString>& factors()
{
    static const QHash<int, QString> map = {
        {1,  QStringLiteral("Химический")},
        {2,  QStringLiteral("Биологический")},
        {3,  QStringLiteral("Аэрозоли ПФД")},
        {4,  QStringLiteral("Шум")},
        {5,  QStringLiteral("Инфразвук")},
        {6,  QStringLiteral("Ультразвук")},
        {7,  QStringLiteral("Вибрация общая")},
        {8,  QStringLiteral("Вибрация локальная")},
        {9,  QStringLiteral("ЭМП")},
        {10, QStringLiteral("Ионизирующие излучения")},
        {11, QStringLiteral("Микроклимат")},
        {12, QStringLiteral("Освещение")},
        {13, QStringLiteral("Тяжесть")},
        {14, QStringLiteral("Напряжённость")},
        {15, QStringLiteral("Травмоопасность")},
        {16, QStringLiteral("СИЗ")},
        {25, QStringLiteral("Аэроионизация")},
        {26, QStringLiteral("Ультрафиолетовое излучение")},
        {41, QStringLiteral("Лазерное излучение")}
    };
    return map;
}

} // namespace

QDate parseDate(const QString& raw)
{
    QString s = raw.trimmed();
    if (s.isEmpty())
        return {};

    // ISO-дата с временем: 2024-05-12T00:00:00
    if (s.contains(QLatin1Char('T')))
        s = s.section(QLatin1Char('T'), 0, 0);
    // Некоторые ODBC-драйверы возвращают дату через пробел:
    // 2024-05-12 00:00:00
    else if (s.size() > 10 && s.at(4) == QLatin1Char('-')
             && s.at(7) == QLatin1Char('-') && s.at(10) == QLatin1Char(' ')) {
        s = s.left(10);
    }

    static const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("d.M.yyyy"),
        QStringLiteral("dd.MM.yy"),
        QStringLiteral("yyyy/MM/dd"),
        QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("dd-MM-yyyy"),
        QStringLiteral("yyyyMMdd")
    };

    for (const QString& f : formats) {
        const QDate d = QDate::fromString(s, f);
        if (d.isValid())
            return d;
    }

    // Последняя попытка — локальный формат.
    const QDate d = QLocale().toDate(s, QLocale::ShortFormat);
    return d;
}

QString normalizeDate(const QVariant& value)
{
    if (!value.isValid() || value.isNull())
        return {};

    if (value.metaType().id() == QMetaType::QDate) {
        const QDate d = value.toDate();
        return d.isValid() ? d.toString(Qt::ISODate) : QString();
    }
    if (value.metaType().id() == QMetaType::QDateTime) {
        const QDateTime dt = value.toDateTime();
        return dt.isValid() ? dt.date().toString(Qt::ISODate) : QString();
    }

    const QString raw = value.toString().trimmed();
    if (raw.isEmpty())
        return {};

    const QDate d = parseDate(raw);
    return d.isValid() ? d.toString(Qt::ISODate) : raw;
}

QString formatDateRu(const QString& raw)
{
    if (raw.trimmed().isEmpty())
        return QStringLiteral("—");

    const QDate d = parseDate(raw);
    return d.isValid() ? d.toString(QStringLiteral("dd.MM.yyyy")) : raw;
}

QString factorName(int id)
{
    return factors().value(id, QString::number(id));
}

QString decodeFactors(const QString& facIds)
{
    const QString trimmed = facIds.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("Не указаны");

    // В MDB факторы хранятся в виде "-1-11-": перед каждым номером стоит
    // дефис-разделитель. Извлекаем все числа, как это делает Grafic.py.
    static const QRegularExpression numberPattern(QStringLiteral("\\d+"));
    QStringList names;
    QSet<int> seen;
    auto matches = numberPattern.globalMatch(trimmed);
    while (matches.hasNext()) {
        bool ok = false;
        const int id = matches.next().captured(0).toInt(&ok);
        if (!ok || id <= 0 || seen.contains(id))
            continue;
        seen.insert(id);
        names << factorName(id);
    }

    if (names.isEmpty()) {
        // Строка без номеров — уже готовое текстовое описание.
        return trimmed;
    }
    return names.join(QStringLiteral(", "));
}

DeviceUtils::IndicatorBucket bucket(bool active, const QString& endDate)
{
    const QDate end = parseDate(endDate);
    if (!end.isValid())
        return Unknown;

    const int left = QDate::currentDate().daysTo(end);
    if (!active || left < 0)
        return Expired;
    if (left <= 30)
        return Expiring;
    return Valid;
}

int daysLeft(const QString& endDate)
{
    const QDate end = parseDate(endDate);
    if (!end.isValid())
        return INT_MIN;
    return QDate::currentDate().daysTo(end);
}

QString indicatorShort(IndicatorBucket bucket)
{
    switch (bucket) {
    case Expired:  return QStringLiteral("✖");
    case Expiring: return QStringLiteral("⚠");
    case Valid:    return QStringLiteral("✔");
    case Unknown:  break;
    }
    return QStringLiteral("—");
}

QString indicatorText(IndicatorBucket bucket)
{
    switch (bucket) {
    case Expired:  return QStringLiteral("Просрочена");
    case Expiring: return QStringLiteral("Истекает");
    case Valid:    return QStringLiteral("Действует");
    case Unknown:  break;
    }
    return QStringLiteral("Нет данных");
}

QString indicatorTooltip(bool active, const QString& endDate)
{
    Q_UNUSED(active)
    const QDate end = parseDate(endDate);
    if (!end.isValid())
        return QStringLiteral("Нет сведений о поверке");

    const int left = QDate::currentDate().daysTo(end);
    if (left < 0) {
        return QStringLiteral("Поверка истекла %1 (%2 дн. назад)")
            .arg(end.toString(QStringLiteral("dd.MM.yyyy")))
            .arg(-left);
    }
    if (left == 0)
        return QStringLiteral("Поверка заканчивается сегодня");
    return QStringLiteral("Поверка действует до %1 (осталось %2 дн.)")
        .arg(end.toString(QStringLiteral("dd.MM.yyyy")))
        .arg(left);
}

QColor indicatorColor(IndicatorBucket bucket)
{
    switch (bucket) {
    case Expired:  return QColor(QStringLiteral("#b91c1c"));
    case Expiring: return QColor(QStringLiteral("#b45309"));
    case Valid:    return QColor(QStringLiteral("#047857"));
    case Unknown:  break;
    }
    return QColor(QStringLiteral("#64748b"));
}

QColor indicatorBackground(IndicatorBucket bucket)
{
    switch (bucket) {
    case Expired:  return QColor(QStringLiteral("#fee2e2"));
    case Expiring: return QColor(QStringLiteral("#fef3c7"));
    case Valid:    return QColor(QStringLiteral("#d1fae5"));
    case Unknown:  break;
    }
    return QColor(QStringLiteral("#eef2f7"));
}

} // namespace DeviceUtils
