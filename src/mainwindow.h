#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QCheckBox>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QScrollArea>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTimer>
#include <QKeyEvent>
#include "reportmanager.h"
#include "reportmodel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void createReport();
    void refreshAllLists();
    void onUserSelectionChanged();
    void onAdminSelectionChanged();
    void onArchiveDoubleClick(const QModelIndex& index);
    void sendUserMessage();
    void sendAdminMessage();
    void resolveSelected();
    void saveSettings();
    void openAttachment(const QString& path);
    void autoRefresh();

private:
    QWidget* buildUserTab();
    QWidget* buildAdminTab();
    QWidget* buildArchiveTab();
    QWidget* buildRepoTab();
    QWidget* buildSettingsTab();
    
    void buildChatUI(QWidget* parentWidget, QVBoxLayout* chatLayout, const Report& r, bool isAdminView);
    void renderAttachments(QHBoxLayout* layout, const Report& r);
    void updateTabsUnreadCount();
    void updateAdminTabVisibility(); // <--- НОВЫЙ МЕТОД
    void showArchiveReader(const Report& r);

    QTabWidget* m_tabs;
    QTimer* m_refreshTimer;

    // --- Вкладка "Мой отчет" ---
    QLineEdit* m_userTitleEdit;
    QTextEdit* m_userDescEdit;
    QStringList m_pendingImages;
    QTableView* m_userTable;
    ReportModel* m_userModel;
    QSortFilterProxyModel* m_userProxy;
    QLabel* m_userDetail;
    QWidget* m_userAttachWidget;
    QHBoxLayout* m_userAttachLayout;
    QScrollArea* m_userChatScroll;
    QWidget* m_userChatWidget;
    QVBoxLayout* m_userChatLayout;
    QLineEdit* m_userMsgInput;
    QString m_currentUserId;

    // --- Вкладка "Админ" ---
    QWidget* m_adminTabWidget; // <--- Ссылка на саму вкладку для скрытия
    QTableView* m_adminTable;
    ReportModel* m_adminModel;
    QSortFilterProxyModel* m_adminProxy;
    QLabel* m_adminDetail;
    QWidget* m_adminAttachWidget;
    QHBoxLayout* m_adminAttachLayout;
    QScrollArea* m_adminChatScroll;
    QWidget* m_adminChatWidget;
    QVBoxLayout* m_adminChatLayout;
    QLineEdit* m_adminMsgInput;
    QString m_currentAdminId;

    // --- Вкладка "Архив" ---
    QTableView* m_archiveTable;
    ReportModel* m_archiveModel;
    QSortFilterProxyModel* m_archiveProxy;

    // --- Вкладка "Настройки" ---
    QLineEdit* m_pathEdit;
    QLineEdit* m_pcEdit;
    QCheckBox* m_showAllChk;

    ReportManager m_rm;
};