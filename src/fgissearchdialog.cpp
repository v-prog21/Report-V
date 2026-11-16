#include "fgissearchdialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

QString nonEmpty(const QString& value, const QString& fallback = QStringLiteral("—"))
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

} // namespace

FgisSearchDialog::FgisSearchDialog(QWidget* parent)
    : QDialog(parent)
    , m_network(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("Поиск в ФГИС «Аршин»"));
    resize(980, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* hint = new QLabel(QStringLiteral(
        "Поиск выполняется через внешний публичный интерфейс ФГИС «Аршин» "
        "(eapi/vri). Введите номер свидетельства, заводской номер, тип СИ "
        "или часть наименования."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText(QStringLiteral("Например: 1024-25 или номер СИ"));
    m_queryEdit->setClearButtonEnabled(true);
    m_searchButton = new QPushButton(QStringLiteral("Найти"));
    m_searchButton->setDefault(true);
    searchRow->addWidget(m_queryEdit, 1);
    searchRow->addWidget(m_searchButton);
    root->addLayout(searchRow);

    m_statusLabel = new QLabel(QStringLiteral("Введите запрос и нажмите «Найти»."));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    m_table = new QTableWidget;
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Номер СИ"),
        QStringLiteral("Тип / модификация"),
        QStringLiteral("Дата поверки"),
        QStringLiteral("Действует до"),
        QStringLiteral("Номер документа"),
        QStringLiteral("Организация"),
        QStringLiteral("vri_id")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    m_openButton = buttons->addButton(QStringLiteral("Открыть карточку"),
                                      QDialogButtonBox::ActionRole);
    m_openButton->setEnabled(false);
    root->addWidget(buttons);

    connect(m_searchButton, &QPushButton::clicked, this, &FgisSearchDialog::startSearch);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, &FgisSearchDialog::startSearch);
    connect(m_openButton, &QPushButton::clicked, this, &FgisSearchDialog::openSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &FgisSearchDialog::onRowActivated);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        m_openButton->setEnabled(m_table->currentRow() >= 0
                                 && m_table->currentRow() < m_results.size());
    });
}

void FgisSearchDialog::setInitialQuery(const QString& text)
{
    m_queryEdit->setText(text.trimmed());
    m_queryEdit->selectAll();
}

void FgisSearchDialog::startSearch()
{
    const QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Введите поисковый запрос."));
        return;
    }
    performSearch(query);
}

void FgisSearchDialog::performSearch(const QString& query, int start)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://fgis.gost.ru/fundmetrology/eapi/vri"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("search"), QStringLiteral("*%1*").arg(query));
    params.addQueryItem(QStringLiteral("rows"), QStringLiteral("50"));
    params.addQueryItem(QStringLiteral("start"), QString::number(start));
    url.setQuery(params);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SupportIL/2.0"));
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_searchButton->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Выполняю поиск в ФГИС «Аршин»…"));
    QApplication::setOverrideCursor(Qt::BusyCursor);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &FgisSearchDialog::onSearchFinished);
}

void FgisSearchDialog::onSearchFinished()
{
    QApplication::restoreOverrideCursor();
    m_searchButton->setEnabled(true);

    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_statusLabel->setText(QStringLiteral(
            "Не удалось выполнить запрос: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        m_statusLabel->setText(QStringLiteral("Получен некорректный ответ от ФГИС."));
        return;
    }

    const QJsonObject result = doc.object().value(QStringLiteral("result")).toObject();
    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    const int count = result.value(QStringLiteral("count")).toInt(items.size());

    populateTable(items);

    if (m_results.isEmpty()) {
        m_statusLabel->setText(QStringLiteral(
            "Ничего не найдено. Попробуйте изменить запрос (например, указать "
            "часть номера документа или заводской номер)."));
    } else {
        m_statusLabel->setText(QStringLiteral(
            "Найдено записей: %1 (показано %2). Дважды нажмите строку, чтобы "
            "открыть карточку поверки.")
                                   .arg(count)
                                   .arg(m_results.size()));
    }
}

QString FgisSearchDialog::valueToString(const QJsonValue& value)
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

QString FgisSearchDialog::fieldString(const QJsonObject& object, const char* key)
{
    return valueToString(object.value(QLatin1String(key))).trimmed();
}

void FgisSearchDialog::populateTable(const QJsonArray& items)
{
    m_results.clear();
    m_table->setRowCount(0);

    for (const QJsonValue& value : items) {
        if (!value.isObject())
            continue;
        const QJsonObject o = value.toObject();

        FgisSearchResult item;
        item.vriId = fieldString(o, "vri_id");
        if (item.vriId.isEmpty())
            item.vriId = fieldString(o, "vriInfoId");
        if (item.vriId.isEmpty())
            continue;

        item.miNumber = fieldString(o, "mi_number");
        if (item.miNumber.isEmpty())
            item.miNumber = fieldString(o, "number_si");

        item.orgTitle = fieldString(o, "org_title");
        item.verificationDate = fieldString(o, "verification_date");
        item.validDate = fieldString(o, "valid_date");
        item.resultDocnum = fieldString(o, "result_docnum");
        if (item.resultDocnum.isEmpty())
            item.resultDocnum = fieldString(o, "applicible_docnum");

        item.miType = fieldString(o, "mi_mitnumber");
        if (item.miType.isEmpty())
            item.miType = fieldString(o, "mi_mitype");
        item.miModification = fieldString(o, "mi_modification");
        item.applicant = fieldString(o, "applicant");

        const int row = m_table->rowCount();
        m_table->insertRow(row);

        const QString typeText = item.miModification.isEmpty()
                                     ? item.miType
                                     : (item.miType.isEmpty()
                                            ? item.miModification
                                            : item.miType + QStringLiteral(", ")
                                                  + item.miModification);

        m_table->setItem(row, 0, new QTableWidgetItem(nonEmpty(item.miNumber)));
        m_table->setItem(row, 1, new QTableWidgetItem(nonEmpty(typeText)));
        m_table->setItem(row, 2, new QTableWidgetItem(nonEmpty(item.verificationDate)));
        m_table->setItem(row, 3, new QTableWidgetItem(nonEmpty(item.validDate)));
        m_table->setItem(row, 4, new QTableWidgetItem(nonEmpty(item.resultDocnum)));
        m_table->setItem(row, 5, new QTableWidgetItem(nonEmpty(item.orgTitle)));
        m_table->setItem(row, 6, new QTableWidgetItem(item.vriId));

        for (int col = 0; col < m_table->columnCount(); ++col) {
            if (auto* cell = m_table->item(row, col))
                cell->setToolTip(QStringLiteral("Прямая ссылка: https://fgis.gost.ru/fundmetrology/cm/results/%1")
                                     .arg(item.vriId));
        }

        m_results << item;
    }

    m_openButton->setEnabled(m_table->rowCount() > 0);
    if (m_table->rowCount() > 0)
        m_table->selectRow(0);
}

void FgisSearchDialog::onRowActivated(int row, int column)
{
    Q_UNUSED(column)
    if (row < 0 || row >= m_results.size())
        return;
    openCard(m_results.at(row));
}

void FgisSearchDialog::openSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_results.size())
        return;
    openCard(m_results.at(row));
}

void FgisSearchDialog::openCard(const FgisSearchResult& result)
{
    if (result.vriId.isEmpty())
        return;
    const QUrl url(QStringLiteral("https://fgis.gost.ru/fundmetrology/cm/results/%1")
                       .arg(result.vriId));
    QDesktopServices::openUrl(url);
    m_statusLabel->setText(QStringLiteral("Открыта карточка поверки: %1").arg(url.toString()));
}
