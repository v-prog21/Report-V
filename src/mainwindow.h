#pragma once

#include <QMainWindow>
#include <QSortFilterProxyModel>
#include <QTimer>

#include "networkmanager.h"
#include "reportmanager.h"
#include "reportmodel.h"

class QCheckBox;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTextEdit;
class QVBoxLayout;
class QWidget;

class DevicesWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void startDirectChat();
    void createGroupChat();
    void addGroupMembers();
    void onPeerActivated(QListWidgetItem* item);
    void refreshAllLists();
    void onActiveSelectionChanged();
    void onArchiveSelectionChanged();
    void sendMessage();
    void attachFiles();
    void archiveSelected();
    void deleteSelectedArchive();
    void saveSettings();
    void autoRefresh();
    void onRosterChanged();
    void onRemoteMessage(const QJsonObject& payload);
    void onHistoryReceived(const QJsonArray& items);
    void onRemoteArchive(const QString& id);
    void onRemoteDelete(const QString& id);
    void onDownloadFinished(const QString& filename, const QString& path,
                            bool ok, const QString& error);

private:
    QWidget* buildHeaderBar();
    QWidget* buildMessengerTab();
    QWidget* buildArchiveTab();
    QWidget* buildRepoTab();
    QWidget* buildSettingsTab();

    void rebuildChat(QVBoxLayout* chatLayout, QScrollArea* scroll,
                     const Report& report, bool readOnly);
    void appendChatBubble(QVBoxLayout* chatLayout, QScrollArea* scroll,
                          const Report& report, const Message& message,
                          bool readOnly);
    void clearChatLayout(QVBoxLayout* layout);
    void scrollChatToBottom(QScrollArea* scroll);
    void showChatPlaceholder(QVBoxLayout* layout, const QString& text);
    void renderAttachments(QHBoxLayout* layout, const Report& report,
                           const QList<Attachment>& attachments,
                           bool readOnly);
    QWidget* createAttachmentChip(const Report& report, const Attachment& attachment,
                                  bool readOnly);

    bool restoreSelection(QTableView* table, QSortFilterProxyModel* proxy,
                          ReportModel* model, const QString& reportId);
    void displayActiveChat();
    void displayArchiveChat();
    void clearActiveChat();
    void clearArchiveChat();
    void updateTabsUnreadCount();
    void updateHeaderStatus();
    void updateSettingsInfo();
    void updatePendingFilesUi();
    void updateActiveActions();
    void updateArchiveActions();

    void refreshPeerList();
    void handleAttachmentAction(const Report& report, const Attachment& attachment);
    void maybeStartNetwork();
    void syncPeerNames();
    static QString peerLabel(const PeerInfo& peer);

    ReportManager m_rm;
    NetworkManager m_network;

    QTabWidget* m_tabs = nullptr;
    QTimer* m_refreshTimer = nullptr;
    qint64 m_lastRevision = -1;

    // Шапка
    QLabel* m_headerPcLabel = nullptr;
    QLabel* m_headerStatusLabel = nullptr;
    QLabel* m_dbStatusLabel = nullptr;

    // --- Мессенджер ---
    QWidget* m_messengerTab = nullptr;
    QLabel* m_selfLabel = nullptr;
    QListWidget* m_peerList = nullptr;
    QPushButton* m_startChatButton = nullptr;
    QPushButton* m_createGroupButton = nullptr;

    QTableView* m_chatTable = nullptr;
    ReportModel* m_chatModel = nullptr;
    QSortFilterProxyModel* m_chatProxy = nullptr;

    QLabel* m_chatDetail = nullptr;
    QWidget* m_chatAttachWidget = nullptr;
    QHBoxLayout* m_chatAttachLayout = nullptr;
    QScrollArea* m_chatScroll = nullptr;
    QWidget* m_chatCanvas = nullptr;
    QVBoxLayout* m_chatLayout = nullptr;
    QLineEdit* m_messageInput = nullptr;
    QPushButton* m_sendButton = nullptr;
    QPushButton* m_attachButton = nullptr;
    QPushButton* m_clearFilesButton = nullptr;
    QPushButton* m_addMembersButton = nullptr;
    QPushButton* m_archiveButton = nullptr;
    QLabel* m_pendingFilesLabel = nullptr;
    QStringList m_pendingFiles;
    QString m_currentChatId;

    // --- Архив ---
    QWidget* m_archiveTab = nullptr;
    QTableView* m_archiveTable = nullptr;
    ReportModel* m_archiveModel = nullptr;
    QSortFilterProxyModel* m_archiveProxy = nullptr;
    QLabel* m_archiveDetail = nullptr;
    QWidget* m_archiveAttachWidget = nullptr;
    QHBoxLayout* m_archiveAttachLayout = nullptr;
    QScrollArea* m_archiveScroll = nullptr;
    QWidget* m_archiveCanvas = nullptr;
    QVBoxLayout* m_archiveLayout = nullptr;
    QPushButton* m_archiveDeleteButton = nullptr;
    QString m_currentArchiveId;

    // --- Приборы ---
    DevicesWidget* m_devicesWidget = nullptr;

    // --- Настройки ---
    QWidget* m_settingsTab = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_mdbPathEdit = nullptr;
    QLineEdit* m_devicesDbPathEdit = nullptr;
    QLineEdit* m_reportsDbPathEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QLabel* m_settingsStatsLabel = nullptr;
    QLabel* m_settingsMdbStatusLabel = nullptr;
};
