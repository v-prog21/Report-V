#include "fgisclient.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

FgisClient::FgisClient(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

QString FgisClient::valueToString(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toVariant().toLongLong());
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        if (o.contains(QStringLiteral("value")))
            return valueToString(o.value(QStringLiteral("value")));
        if (o.contains(QStringLiteral("title")))
            return valueToString(o.value(QStringLiteral("title")));
    }
    return {};
}

QString FgisClient::directCardUrl(const QString& vriId)
{
    return QStringLiteral("https://fgis.gost.ru/fundmetrology/cm/results/%1").arg(vriId.trimmed());
}

QString FgisClient::searchPageUrl(const QString& query)
{
    QUrl url(QStringLiteral("https://fgis.gost.ru/fundmetrology/cm/results"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("filter_mi_number"), query);
    url.setQuery(q);
    return url.toString();
}

void FgisClient::search(const QString& query, int rows)
{
    const QString text = query.trimmed();
    if (text.isEmpty()) {
        emit searchFinished({}, QStringLiteral("Пустой поисковый запрос."));
        return;
    }

    QUrl url(QStringLiteral("https://fgis.gost.ru/fundmetrology/eapi/vri"));
    QUrlQuery params;
    // Публичный API поддерживает шаблон *...* для поиска по атрибутам.
    params.addQueryItem(QStringLiteral("search"), QStringLiteral("*%1*").arg(text));
    params.addQueryItem(QStringLiteral("rows"), QString::number(qMax(1, rows)));
    params.addQueryItem(QStringLiteral("start"), QStringLiteral("0"));
    url.setQuery(params);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SupportIL/2.0"));
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit searchFinished({}, reply->errorString());
            return;
        }

        const QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
        const QJsonArray items = json.object()
                                     .value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("items")).toArray();

        QList<FgisVriResult> results;
        results.reserve(items.size());
        for (const QJsonValue& value : items) {
            const QJsonObject o = value.toObject();
            FgisVriResult item;
            item.vriId = valueToString(o.value(QStringLiteral("vri_id")));
            if (item.vriId.isEmpty())
                item.vriId = valueToString(o.value(QStringLiteral("vriInfoId")));
            if (item.vriId.isEmpty())
                continue;
            item.miNumber = valueToString(o.value(QStringLiteral("mi_number")));
            item.resultDocnum = valueToString(o.value(QStringLiteral("result_docnum")));
            item.verificationDate = valueToString(o.value(QStringLiteral("verification_date")));
            item.validDate = valueToString(o.value(QStringLiteral("valid_date")));
            item.orgTitle = valueToString(o.value(QStringLiteral("org_title")));
            results << item;
        }

        emit searchFinished(results, QString());
    });
}

void FgisClient::openDocument(const QString& numDoc)
{
    const QString doc = numDoc.trimmed();
    if (doc.isEmpty()) {
        emit statusMessage(QStringLiteral("Не указан номер документа поверки."));
        return;
    }

    QUrl url(QStringLiteral("https://fgis.gost.ru/fundmetrology/eapi/vri"));
    QUrlQuery query;
    // Ищем по номеру документа (result_docnum / общий search) и открываем
    // прямую ссылку на карточку найденной записи.
    query.addQueryItem(QStringLiteral("search"), QStringLiteral("*%1*").arg(doc));
    query.addQueryItem(QStringLiteral("rows"), QStringLiteral("10"));
    query.addQueryItem(QStringLiteral("start"), QStringLiteral("0"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SupportIL/2.0"));
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    emit statusMessage(QStringLiteral("Ищу поверку %1 в ФГИС «Аршин»…").arg(doc));

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, doc] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            openFallback(doc);
            return;
        }

        const QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
        const QJsonArray items = json.object()
                                     .value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            openFallback(doc);
            return;
        }

        // Предпочитаем точное совпадение номера документа.
        QString chosenId;
        for (const QJsonValue& value : items) {
            const QJsonObject o = value.toObject();
            const QString id = valueToString(o.value(QStringLiteral("vri_id")));
            const QString docNum = valueToString(o.value(QStringLiteral("result_docnum")));
            if (id.isEmpty())
                continue;
            if (docNum.compare(doc, Qt::CaseInsensitive) == 0) {
                chosenId = id;
                break;
            }
            if (chosenId.isEmpty())
                chosenId = id;
        }

        if (chosenId.isEmpty()) {
            openFallback(doc);
            return;
        }

        openVriCard(chosenId);
    });
}

void FgisClient::openFallback(const QString& numDoc)
{
    QUrl url(QStringLiteral("https://fgis.gost.ru/fundmetrology/cm/results"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("filter_mi_number"), numDoc);
    url.setQuery(query);

    emit statusMessage(QStringLiteral(
        "Прямая карточка не найдена, открываю поиск по номеру %1.").arg(numDoc));
    QDesktopServices::openUrl(url);
}

void FgisClient::openVriCard(const QString& vriId)
{
    const QUrl url(directCardUrl(vriId));
    emit statusMessage(QStringLiteral("Открываю карточку поверки в ФГИС «Аршин»."));
    QDesktopServices::openUrl(url);
}
