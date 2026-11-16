#pragma once

#include <QColor>
#include <QDate>
#include <QString>
#include <QStringList>
#include <QVariant>

/// Общие помощники для работы с приборами: даты, факторы, индикаторы поверки.
namespace DeviceUtils {

enum IndicatorBucket {
    Expired  = 0,   // поверка истекла
    Expiring = 1,   // истекает в ближайшие 30 дней
    Valid    = 2,   // поверка действует
    Unknown  = 3    // нет данных о поверке
};

QDate parseDate(const QString& raw);
QString normalizeDate(const QVariant& value);
QString formatDateRu(const QString& raw);

QString decodeFactors(const QString& facIds);
QString factorName(int id);

IndicatorBucket bucket(bool active, const QString& endDate);
int daysLeft(const QString& endDate);

QString indicatorShort(IndicatorBucket bucket);
QString indicatorText(IndicatorBucket bucket);
QString indicatorTooltip(bool active, const QString& endDate);
QColor indicatorColor(IndicatorBucket bucket);
QColor indicatorBackground(IndicatorBucket bucket);

} // namespace DeviceUtils
