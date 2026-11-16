#pragma once

#include <QPoint>
#include <QWidget>

#include "devicerepository.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTimer;

template <typename T> class QFutureWatcher;

class DeviceTableModel;
class DeviceFilterProxyModel;
class FgisClient;
class FgisSearchDialog;

class DevicesWidget : public QWidget {
    Q_OBJECT
public:
    explicit DevicesWidget(QWidget* parent = nullptr);

    void reloadFromCache();
    void startImport(bool interactive);
    void onSettingsChanged();
    void optimizeDatabase();

    int cachedDeviceCount() const;
    QString lastSyncText() const;
    bool isImporting() const { return m_importing; }

signals:
    void requestSettings();
    void statusMessage(const QString& message);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildToolbar();
    QWidget* buildBottomBar();
    QWidget* buildEmptyPage();

    void applySearch(const QString& text);
    void openDetailsForCurrent();
    void openFgisForCurrent();
    void openFgisSearch();
    void showContextMenu(const QPoint& position);
    void setImporting(bool importing, const QString& message = QString());
    void updateStats();
    void updateEmptyState();
    void updateSelectionActions();
    void finishImport(const DeviceImportResult& result);

    DeviceRepository m_repo;
    DeviceTableModel* m_model = nullptr;
    DeviceFilterProxyModel* m_proxy = nullptr;
    QTableView* m_table = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_statusFilter = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_fgisSearchButton = nullptr;
    QPushButton* m_detailsButton = nullptr;
    QPushButton* m_fgisButton = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_syncLabel = nullptr;
    QLabel* m_emptyStatus = nullptr;
    QProgressBar* m_progress = nullptr;
    QStackedWidget* m_stack = nullptr;
    QTimer* m_searchTimer = nullptr;
    FgisClient* m_fgis = nullptr;
    QFutureWatcher<DeviceImportResult>* m_importWatcher = nullptr;

    bool m_importing = false;
    bool m_importInteractive = false;
    bool m_autoImportChecked = false;
};
