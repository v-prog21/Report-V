#pragma once

#include <QObject>
#include <QStringList>

class QNetworkAccessManager;

struct FgisVriResult {
    QString vriId;
    QString miNumber;
    QString resultDocnum;
    QString verificationDate;
    QString validDate;
    QString orgTitle;
};

/// Поиск карточки поверки в ФГИС «Аршин» по номеру документа.
/// Реализует публичный интерфейс eapi/vri согласно руководству пользователя.
class FgisClient : public QObject {
    Q_OBJECT
public:
    explicit FgisClient(QObject* parent = nullptr);

    /// Открывает прямую карточку cm/results/{vri_id} для найденного документа.
    void openDocument(const QString& numDoc);

    /// Выполняет поиск через eapi/vri и возвращает результаты сигналом.
    void search(const QString& query, int rows = 30);

    static QString directCardUrl(const QString& vriId);
    static QString searchPageUrl(const QString& query);

signals:
    void statusMessage(const QString& message);
    void searchFinished(const QList<FgisVriResult>& results, const QString& error);

private:
    void openFallback(const QString& numDoc);
    void openVriCard(const QString& vriId);
    static QString valueToString(const QJsonValue& value);

    QNetworkAccessManager* m_network;
};
