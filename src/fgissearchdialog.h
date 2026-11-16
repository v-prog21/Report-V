#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QList>

class QLineEdit;
class QPushButton;
class QTableWidget;
class QLabel;

class QNetworkAccessManager;
class QNetworkReply;

struct FgisSearchResult {
    QString vriId;
    QString miNumber;
    QString orgTitle;
    QString verificationDate;
    QString validDate;
    QString resultDocnum;
    QString miType;
    QString miModification;
    QString applicant;
};

class FgisSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit FgisSearchDialog(QWidget* parent = nullptr);
    ~FgisSearchDialog() override;

    void setInitialQuery(const QString& text);

private slots:
    void startSearch();
    void onSearchFinished();
    void onRowActivated(int row, int column);
    void openSelected();

private:
    void performSearch(const QString& query, int start = 0);
    void populateTable(const QJsonArray& items);
    void openCard(const FgisSearchResult& result);
    static QString valueToString(const QJsonValue& value);
    static QString fieldString(const QJsonObject& object, const char* key);

    QLineEdit* m_queryEdit = nullptr;
    QPushButton* m_searchButton = nullptr;
    QPushButton* m_openButton = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_statusLabel = nullptr;

    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    QList<FgisSearchResult> m_results;
};
