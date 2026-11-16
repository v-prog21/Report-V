#include "mainwindow.h"

#include "deviceswidget.h"
#include "settingsmanager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

QString formatSize(qint64 bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 Б").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 КБ").arg(bytes / 1024);
    return QStringLiteral("%1 МБ").arg(bytes / (1024 * 1024));
}

QFrame* makeHLine()
{
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setObjectName(QStringLiteral("hline"));
    return line;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_rm(SettingsManager::instance().reportsPath())
{
    setWindowTitle(QStringLiteral("Support IL"));
    resize(1280, 820);
    setMinimumSize(980, 680);

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildHeaderBar());

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);

    m_messengerTab = buildMessengerTab();
    m_archiveTab = buildArchiveTab();
    m_devicesWidget = new DevicesWidget;
    m_settingsTab = buildSettingsTab();

    m_tabs->addTab(m_devicesWidget, QStringLiteral("Приборы"));
    m_tabs->addTab(m_messengerTab, QStringLiteral("Мессенджер"));
    m_tabs->addTab(m_archiveTab, QStringLiteral("Архив"));
    m_tabs->addTab(buildRepoTab(), QStringLiteral("Фишки"));
    m_tabs->addTab(m_settingsTab, QStringLiteral("Настройки"));

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (m_tabs->currentWidget() != m_messengerTab)
            return;
        if (m_currentChatId.isEmpty() && m_chatModel->rowCount() > 0) {
            m_currentChatId = m_chatModel->at(0).id;
            QSignalBlocker blocker(m_chatTable->selectionModel());
            restoreSelection(m_chatTable, m_chatProxy, m_chatModel, m_currentChatId);
        }
        if (!m_currentChatId.isEmpty())
            displayActiveChat();
    });

    connect(m_devicesWidget, &DevicesWidget::requestSettings, this, [this] {
        m_tabs->setCurrentWidget(m_settingsTab);
    });
    connect(m_devicesWidget, &DevicesWidget::statusMessage, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 6000);
    });

    root->addWidget(m_tabs, 1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Готово"));

    m_network.setHistoryProvider([this](const QString& requester) {
        return m_rm.historyPayloadsFor(requester);
    });
    m_network.setAttachmentResolver([this](const QString& filename) {
        return m_rm.getAttachmentPath(filename);
    });

    connect(&m_network, &NetworkManager::rosterChanged, this, &MainWindow::onRosterChanged);
    connect(&m_network, &NetworkManager::remoteMessage, this, &MainWindow::onRemoteMessage);
    connect(&m_network, &NetworkManager::historyReceived, this, &MainWindow::onHistoryReceived);
    connect(&m_network, &NetworkManager::conversationArchived, this, &MainWindow::onRemoteArchive);
    connect(&m_network, &NetworkManager::conversationDeleted, this, &MainWindow::onRemoteDelete);
    connect(&m_network, &NetworkManager::statusMessage, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
    });
    connect(&m_network, &NetworkManager::downloadFinished,
            this, &MainWindow::onDownloadFinished);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2500);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::autoRefresh);
    m_refreshTimer->start();

    maybeStartNetwork();
    refreshAllLists();
    updateSettingsInfo();
    updateTabsUnreadCount();
    updateHeaderStatus();
}

MainWindow::~MainWindow()
{
    m_network.stop();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_network.stop();
    QMainWindow::closeEvent(event);
}

void MainWindow::maybeStartNetwork()
{
    if (!m_network.isRunning()) {
        if (!m_network.start())
            statusBar()->showMessage(QStringLiteral("Сетевая служба не запущена."), 6000);
    }
    refreshPeerList();
}

QWidget* MainWindow::buildHeaderBar()
{
    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("headerBar"));
    bar->setFixedHeight(52);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Support IL"));
    title->setObjectName(QStringLiteral("headerTitle"));
    layout->addWidget(title);

    layout->addStretch(1);

    m_headerPcLabel = new QLabel;
    m_headerPcLabel->setObjectName(QStringLiteral("headerMeta"));
    layout->addWidget(m_headerPcLabel);

    m_headerStatusLabel = new QLabel;
    m_headerStatusLabel->setObjectName(QStringLiteral("headerMeta"));
    layout->addWidget(m_headerStatusLabel);

    m_dbStatusLabel = new QLabel;
    m_dbStatusLabel->setObjectName(QStringLiteral("headerMeta"));
    layout->addWidget(m_dbStatusLabel);

    return bar;
}

QWidget* MainWindow::buildMessengerTab()
{
    auto* tab = new QWidget;

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    // ------------------------- Левая панель -------------------------
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(12, 12, 8, 12);
    leftLayout->setSpacing(10);

    auto* usersGroup = new QGroupBox(QStringLiteral("Пользователи"));
    auto* usersLayout = new QVBoxLayout(usersGroup);

    m_selfLabel = new QLabel;
    m_selfLabel->setObjectName(QStringLiteral("chip"));
    m_selfLabel->setTextFormat(Qt::RichText);
    usersLayout->addWidget(m_selfLabel);

    m_peerList = new QListWidget;
    m_peerList->setObjectName(QStringLiteral("linkList"));
    m_peerList->setMinimumHeight(120);
    usersLayout->addWidget(m_peerList, 1);

    auto* peerButtons = new QHBoxLayout;
    m_startChatButton = new QPushButton(QStringLiteral("Написать"));
    m_createGroupButton = new QPushButton(QStringLiteral("Создать группу"));
    m_createGroupButton->setObjectName(QStringLiteral("btnSecondary"));
    peerButtons->addWidget(m_startChatButton);
    peerButtons->addWidget(m_createGroupButton);
    usersLayout->addLayout(peerButtons);

    leftLayout->addWidget(usersGroup, 2);

    auto* chatsGroup = new QGroupBox(QStringLiteral("Чаты"));
    auto* chatsLayout = new QVBoxLayout(chatsGroup);
    m_chatTable = new QTableView;
    m_chatModel = new ReportModel(this);
    m_chatProxy = new QSortFilterProxyModel(this);
    m_chatProxy->setSourceModel(m_chatModel);
    m_chatProxy->setFilterKeyColumn(-1);
    m_chatProxy->setFilterRole(ReportModel::FilterRole);
    m_chatProxy->setSortRole(ReportModel::SortRole);
    m_chatProxy->setDynamicSortFilter(false);
    m_chatTable->setModel(m_chatProxy);
    m_chatTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_chatTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_chatTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chatTable->setSortingEnabled(true);
    m_chatTable->setItemDelegateForColumn(ReportModel::ColStatus,
                                          new ReportStatusDelegate(m_chatTable));
    m_chatTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColTitle,
                                                          QHeaderView::Stretch);
    m_chatTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColType,
                                                          QHeaderView::ResizeToContents);
    m_chatTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColMessages,
                                                          QHeaderView::ResizeToContents);
    m_chatTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColStatus,
                                                          QHeaderView::ResizeToContents);
    m_chatTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColUpdated,
                                                          QHeaderView::ResizeToContents);
    m_chatTable->setColumnHidden(ReportModel::ColId, true);
    m_chatTable->verticalHeader()->setVisible(false);
    m_chatTable->sortByColumn(ReportModel::ColUpdated, Qt::DescendingOrder);
    chatsLayout->addWidget(m_chatTable);
    leftLayout->addWidget(chatsGroup, 3);

    connect(m_chatTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onActiveSelectionChanged);
    connect(m_startChatButton, &QPushButton::clicked, this, &MainWindow::startDirectChat);
    connect(m_createGroupButton, &QPushButton::clicked, this, &MainWindow::createGroupChat);
    connect(m_peerList, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onPeerActivated);

    // ------------------------- Правая панель ------------------------
    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 12, 12, 12);
    rightLayout->setSpacing(8);

    m_chatDetail = new QLabel(QStringLiteral("Выберите чат слева или начните переписку."));
    m_chatDetail->setObjectName(QStringLiteral("detailTitle"));
    m_chatDetail->setWordWrap(true);
    m_chatDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(m_chatDetail);

    m_chatAttachWidget = new QWidget;
    m_chatAttachLayout = new QHBoxLayout(m_chatAttachWidget);
    m_chatAttachLayout->setContentsMargins(0, 0, 0, 0);
    m_chatAttachLayout->setSpacing(6);
    m_chatAttachWidget->setVisible(false);
    rightLayout->addWidget(m_chatAttachWidget);

    m_chatScroll = new QScrollArea;
    m_chatScroll->setObjectName(QStringLiteral("chatScroll"));
    m_chatScroll->setWidgetResizable(true);
    m_chatScroll->setFrameShape(QFrame::NoFrame);
    m_chatCanvas = new QWidget;
    m_chatCanvas->setObjectName(QStringLiteral("chatCanvas"));
    m_chatLayout = new QVBoxLayout(m_chatCanvas);
    m_chatLayout->setContentsMargins(10, 8, 10, 8);
    m_chatLayout->setSpacing(6);
    m_chatLayout->setAlignment(Qt::AlignTop);
    showChatPlaceholder(m_chatLayout, QStringLiteral("Сообщений пока нет."));
    m_chatScroll->setWidget(m_chatCanvas);
    rightLayout->addWidget(m_chatScroll, 1);

    m_pendingFilesLabel = new QLabel;
    m_pendingFilesLabel->setObjectName(QStringLiteral("muted"));
    m_pendingFilesLabel->setVisible(false);
    rightLayout->addWidget(m_pendingFilesLabel);

    auto* inputRow = new QHBoxLayout;
    m_attachButton = new QPushButton(QStringLiteral("Файл"));
    m_attachButton->setObjectName(QStringLiteral("btnSecondary"));
    m_attachButton->setToolTip(QStringLiteral("Прикрепить файлы к сообщению"));
    m_clearFilesButton = new QPushButton(QStringLiteral("×"));
    m_clearFilesButton->setObjectName(QStringLiteral("btnGhost"));
    m_clearFilesButton->setToolTip(QStringLiteral("Убрать вложения"));
    m_clearFilesButton->setVisible(false);
    m_messageInput = new QLineEdit;
    m_messageInput->setPlaceholderText(QStringLiteral("Введите сообщение…"));
    m_sendButton = new QPushButton(QStringLiteral("Отправить"));
    m_sendButton->setObjectName(QStringLiteral("btnSend"));
    m_sendButton->setDefault(true);
    m_addMembersButton = new QPushButton(QStringLiteral("Участники"));
    m_addMembersButton->setObjectName(QStringLiteral("btnSecondary"));
    m_addMembersButton->setToolTip(QStringLiteral("Добавить пользователей в групповой чат"));
    m_addMembersButton->setVisible(false);
    m_archiveButton = new QPushButton(QStringLiteral("В архив"));
    m_archiveButton->setObjectName(QStringLiteral("btnSecondary"));
    inputRow->addWidget(m_attachButton);
    inputRow->addWidget(m_clearFilesButton);
    inputRow->addWidget(m_messageInput, 1);
    inputRow->addWidget(m_sendButton);
    inputRow->addWidget(m_addMembersButton);
    inputRow->addWidget(m_archiveButton);
    rightLayout->addLayout(inputRow);

    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::sendMessage);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendMessage);
    connect(m_attachButton, &QPushButton::clicked, this, &MainWindow::attachFiles);
    connect(m_clearFilesButton, &QPushButton::clicked, this, [this] {
        m_pendingFiles.clear();
        updatePendingFilesUi();
    });
    connect(m_archiveButton, &QPushButton::clicked, this, &MainWindow::archiveSelected);
    connect(m_addMembersButton, &QPushButton::clicked, this, &MainWindow::addGroupMembers);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);

    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    updateActiveActions();
    return tab;
}

QWidget* MainWindow::buildArchiveTab()
{
    auto* tab = new QWidget;
    auto* splitter = new QSplitter(Qt::Horizontal);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(12, 12, 8, 12);

    m_archiveTable = new QTableView;
    m_archiveModel = new ReportModel(this);
    m_archiveProxy = new QSortFilterProxyModel(this);
    m_archiveProxy->setSourceModel(m_archiveModel);
    m_archiveProxy->setFilterRole(ReportModel::FilterRole);
    m_archiveProxy->setSortRole(ReportModel::SortRole);
    m_archiveProxy->setDynamicSortFilter(false);
    m_archiveTable->setModel(m_archiveProxy);
    m_archiveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_archiveTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_archiveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_archiveTable->setSortingEnabled(true);
    m_archiveTable->setItemDelegateForColumn(ReportModel::ColStatus,
                                             new ReportStatusDelegate(m_archiveTable));
    m_archiveTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColTitle,
                                                             QHeaderView::Stretch);
    m_archiveTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColType,
                                                             QHeaderView::ResizeToContents);
    m_archiveTable->horizontalHeader()->setSectionResizeMode(ReportModel::ColMessages,
                                                             QHeaderView::ResizeToContents);
    m_archiveTable->setColumnHidden(ReportModel::ColId, true);
    m_archiveTable->verticalHeader()->setVisible(false);
    m_archiveTable->sortByColumn(ReportModel::ColUpdated, Qt::DescendingOrder);
    leftLayout->addWidget(m_archiveTable, 1);

    connect(m_archiveTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onArchiveSelectionChanged);

    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 12, 12, 12);
    rightLayout->setSpacing(8);

    m_archiveDetail = new QLabel(QStringLiteral("Выберите чат в архиве."));
    m_archiveDetail->setWordWrap(true);
    m_archiveDetail->setObjectName(QStringLiteral("detailTitle"));
    rightLayout->addWidget(m_archiveDetail);

    m_archiveAttachWidget = new QWidget;
    m_archiveAttachLayout = new QHBoxLayout(m_archiveAttachWidget);
    m_archiveAttachLayout->setContentsMargins(0, 0, 0, 0);
    m_archiveAttachLayout->setSpacing(6);
    m_archiveAttachWidget->setVisible(false);
    rightLayout->addWidget(m_archiveAttachWidget);

    m_archiveScroll = new QScrollArea;
    m_archiveScroll->setObjectName(QStringLiteral("chatScroll"));
    m_archiveScroll->setWidgetResizable(true);
    m_archiveScroll->setFrameShape(QFrame::NoFrame);
    m_archiveCanvas = new QWidget;
    m_archiveCanvas->setObjectName(QStringLiteral("chatCanvas"));
    m_archiveLayout = new QVBoxLayout(m_archiveCanvas);
    m_archiveLayout->setContentsMargins(10, 8, 10, 8);
    m_archiveLayout->setSpacing(6);
    m_archiveLayout->setAlignment(Qt::AlignTop);
    showChatPlaceholder(m_archiveLayout, QStringLiteral("Сообщений нет."));
    m_archiveScroll->setWidget(m_archiveCanvas);
    rightLayout->addWidget(m_archiveScroll, 1);

    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    m_archiveDeleteButton = new QPushButton(QStringLiteral("Удалить чат"));
    m_archiveDeleteButton->setObjectName(QStringLiteral("btnDanger"));
    actions->addWidget(m_archiveDeleteButton);
    rightLayout->addLayout(actions);

    connect(m_archiveDeleteButton, &QPushButton::clicked,
            this, &MainWindow::deleteSelectedArchive);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);

    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    updateArchiveActions();
    return tab;
}

QWidget* MainWindow::buildRepoTab()
{
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(18, 18, 18, 18);

    auto* label = new QLabel(QStringLiteral(
        "Раздел «Фишки» зарезервирован.\n\n"
        "Здесь можно разместить полезные инструкции, шаблоны и заметки."));
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("muted"));
    layout->addWidget(label);
    layout->addStretch(1);
    return tab;
}

QWidget* MainWindow::buildSettingsTab()
{
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_pathEdit = new QLineEdit(SettingsManager::instance().reportsPath());
    m_nameEdit = new QLineEdit(SettingsManager::instance().pcName());
    m_mdbPathEdit = new QLineEdit(SettingsManager::instance().mdbPath());
    m_devicesDbPathEdit = new QLineEdit(SettingsManager::instance().devicesDbPath());
    m_devicesDbPathEdit->setReadOnly(true);
    m_reportsDbPathEdit = new QLineEdit;
    m_reportsDbPathEdit->setReadOnly(true);
    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(SettingsManager::instance().networkPort());

    auto* pathRow = new QWidget;
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto* browsePath = new QPushButton(QStringLiteral("Обзор…"));
    browsePath->setObjectName(QStringLiteral("btnSecondary"));
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(browsePath);
    connect(browsePath, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Папка данных"), m_pathEdit->text());
        if (!dir.isEmpty())
            m_pathEdit->setText(dir);
    });

    auto* mdbRow = new QWidget;
    auto* mdbLayout = new QHBoxLayout(mdbRow);
    mdbLayout->setContentsMargins(0, 0, 0, 0);
    auto* browseMdb = new QPushButton(QStringLiteral("Обзор…"));
    browseMdb->setObjectName(QStringLiteral("btnSecondary"));
    mdbLayout->addWidget(m_mdbPathEdit, 1);
    mdbLayout->addWidget(browseMdb);
    connect(browseMdb, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("База приборов (MDB/ACCDB)"), m_mdbPathEdit->text(),
            QStringLiteral("Microsoft Access (*.mdb *.accdb);;Все файлы (*.*)"));
        if (!file.isEmpty())
            m_mdbPathEdit->setText(file);
    });

    form->addRow(QStringLiteral("Папка данных:"), pathRow);
    form->addRow(QStringLiteral("Имя пользователя:"), m_nameEdit);
    form->addRow(QStringLiteral("Файл приборов (MDB):"), mdbRow);
    form->addRow(QStringLiteral("Локальный кэш приборов:"), m_devicesDbPathEdit);
    form->addRow(QStringLiteral("База чатов:"), m_reportsDbPathEdit);
    form->addRow(QStringLiteral("Сетевой порт:"), m_portSpin);

    auto* nameHint = new QLabel(QStringLiteral(
        "Имя пользователя видно другим участникам локальной сети и используется "
        "в чатах. Сетевой порт должен быть одинаковым на всех компьютерах "
        "(по умолчанию 39417)."));
    nameHint->setWordWrap(true);
    nameHint->setObjectName(QStringLiteral("muted"));

    layout->addLayout(form);
    layout->addWidget(nameHint);

    m_settingsMdbStatusLabel = new QLabel;
    m_settingsMdbStatusLabel->setWordWrap(true);
    layout->addWidget(m_settingsMdbStatusLabel);

    m_settingsStatsLabel = new QLabel;
    m_settingsStatsLabel->setWordWrap(true);
    m_settingsStatsLabel->setObjectName(QStringLiteral("muted"));
    layout->addWidget(m_settingsStatsLabel);

    layout->addWidget(makeHLine());

    auto* networkHint = new QLabel(QStringLiteral(
        "Обмен сообщениями работает напрямую между компьютерами в локальной "
        "сети — без сервера и без прав администратора. Если пользователи не "
        "видят друг друга, проверьте, что компьютеры в одной подсети и что "
        "брандмауэр Windows разрешает приложению локальную сеть."));
    networkHint->setWordWrap(true);
    networkHint->setObjectName(QStringLiteral("muted"));
    layout->addWidget(networkHint);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* saveButton = new QPushButton(QStringLiteral("Сохранить"));
    buttons->addWidget(saveButton);
    layout->addLayout(buttons);

    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveSettings);
    layout->addStretch(1);
    return tab;
}

// =====================================================================
//  Контакты и создание чатов
// =====================================================================
QString MainWindow::peerLabel(const PeerInfo& peer)
{
    return peer.name.trimmed().isEmpty() ? peer.id.left(8) : peer.name.trimmed();
}

void MainWindow::refreshPeerList()
{
    if (!m_peerList)
        return;

    QSignalBlocker blocker(m_peerList);

    m_peerList->clear();
    if (m_selfLabel) {
        m_selfLabel->setText(QStringLiteral("Вы: <b>%1</b>")
                                 .arg(m_rm.selfName().toHtmlEscaped()));
    }

    QList<PeerInfo> peers = m_network.peers();
    std::sort(peers.begin(), peers.end(), [](const PeerInfo& a, const PeerInfo& b) {
        if (a.online != b.online)
            return a.online > b.online;
        return peerLabel(a).toLower() < peerLabel(b).toLower();
    });

    for (const PeerInfo& peer : peers) {
        const QString dot = peer.online ? QStringLiteral("●") : QStringLiteral("○");
        const QString state = peer.online ? QStringLiteral("в сети")
                                          : QStringLiteral("не в сети");
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  %2 — %3").arg(dot, peerLabel(peer), state));
        item->setData(Qt::UserRole, peer.id);
        item->setData(Qt::UserRole + 1, peer.name);
        item->setForeground(peer.online ? QColor(QStringLiteral("#047857"))
                                        : QColor(QStringLiteral("#64748b")));
        item->setToolTip(peer.online
                             ? QStringLiteral("Дважды щёлкните, чтобы написать")
                             : QStringLiteral("Пользователь появится в сети позже. "
                                              "Чат можно начать сейчас — сообщение "
                                              "доставится автоматически."));
        m_peerList->addItem(item);
    }

    if (peers.isEmpty()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("Другие пользователи пока не найдены.\n"
                           "Запустите Support IL на втором компьютере в этой же сети."));
        item->setForeground(QColor(QStringLiteral("#64748b")));
        item->setFlags(Qt::NoItemFlags);
        m_peerList->addItem(item);
    }
}

void MainWindow::startDirectChat()
{
    QListWidgetItem* item = m_peerList ? m_peerList->currentItem() : nullptr;
    if (!item || item->data(Qt::UserRole).toString().isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Новый чат"),
            QStringLiteral("Выберите пользователя в списке «Пользователи»."));
        return;
    }

    const QString peerId = item->data(Qt::UserRole).toString();
    QString peerName;
    for (const PeerInfo& peer : m_network.peers()) {
        if (peer.id == peerId) {
            peerName = peer.name;
            break;
        }
    }
    if (peerName.trimmed().isEmpty())
        peerName = item->data(Qt::UserRole + 1).toString();

    const QString id = m_rm.ensureDirectChat(peerId, peerName);
    if (id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Новый чат"),
                             QStringLiteral("Не удалось создать чат:\n%1")
                                 .arg(m_rm.lastError()));
        return;
    }

    // Сразу сообщаем собеседнику о новом чате, чтобы он появился у него
    // в списке, даже если сообщений пока нет.
    QSet<QString> targets;
    targets.insert(peerId);
    m_network.sendConversation(m_rm.exportConversation(id), targets);

    m_currentChatId = id;
    refreshAllLists();
    statusBar()->showMessage(
        QStringLiteral("Чат с %1 готов.").arg(peerName.trimmed().isEmpty()
                                                  ? peerId.left(8)
                                                  : peerName),
        4000);
}

void MainWindow::createGroupChat()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Новый групповой чат"));
    dialog.resize(460, 480);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Название группы:"), &dialog));
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(QStringLiteral("Например: Техподдержка"));
    layout->addWidget(nameEdit);

    auto* hint = new QLabel(QStringLiteral("Отметьте участников группы:"), &dialog);
    layout->addWidget(hint);

    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("linkList"));
    QList<PeerInfo> peers = m_network.peers();
    std::sort(peers.begin(), peers.end(), [](const PeerInfo& a, const PeerInfo& b) {
        if (a.online != b.online)
            return a.online > b.online;
        return peerLabel(a).toLower() < peerLabel(b).toLower();
    });
    for (const PeerInfo& peer : peers) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1%2").arg(peerLabel(peer),
                                       peer.online ? QStringLiteral("  ●")
                                                   : QStringLiteral("  ○")),
            list);
        item->setData(Qt::UserRole, peer.id);
        item->setData(Qt::UserRole + 1, peer.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(list, 1);

    if (peers.isEmpty()) {
        auto* empty = new QLabel(QStringLiteral(
            "В сети пока нет других пользователей. Запустите приложение на "
            "втором компьютере и обновите список."), &dialog);
        empty->setWordWrap(true);
        empty->setObjectName(QStringLiteral("muted"));
        layout->addWidget(empty);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Создать"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QList<Participant> participants;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        Participant p;
        p.id = item->data(Qt::UserRole).toString();
        p.name = item->data(Qt::UserRole + 1).toString();
        if (!p.id.isEmpty())
            participants << p;
    }

    if (participants.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Групповой чат"),
                                 QStringLiteral("Отметьте хотя бы одного участника."));
        return;
    }

    const QString id = m_rm.createGroupChat(nameEdit->text(), participants);
    if (id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Групповой чат"),
                             QStringLiteral("Не удалось создать группу:\n%1")
                                 .arg(m_rm.lastError()));
        return;
    }

    QSet<QString> targets = m_rm.participantIds(id);
    targets.remove(m_rm.selfId());
    m_network.sendConversation(m_rm.exportConversation(id), targets);

    m_currentChatId = id;
    refreshAllLists();
    statusBar()->showMessage(QStringLiteral("Групповой чат создан."), 4000);
}

void MainWindow::addGroupMembers()
{
    if (m_currentChatId.isEmpty())
        return;

    const Report chat = m_rm.loadOne(m_currentChatId);
    if (chat.id.isEmpty() || chat.kind != QLatin1String("group"))
        return;

    QSet<QString> currentIds;
    for (const Participant& p : chat.participants)
        currentIds.insert(p.id);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Участники группы"));
    dialog.resize(460, 460);

    auto* layout = new QVBoxLayout(&dialog);
    auto* title = new QLabel(QStringLiteral("<b>%1</b>")
                                 .arg(chat.displayTitle(m_rm.selfId()).toHtmlEscaped()),
                             &dialog);
    layout->addWidget(title);
    auto* hint = new QLabel(QStringLiteral(
        "Отметьте пользователей, которых нужно добавить в чат:"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("linkList"));
    QList<PeerInfo> peers = m_network.peers();
    std::sort(peers.begin(), peers.end(), [](const PeerInfo& a, const PeerInfo& b) {
        if (a.online != b.online)
            return a.online > b.online;
        return peerLabel(a).toLower() < peerLabel(b).toLower();
    });
    for (const PeerInfo& peer : peers) {
        if (peer.id == m_rm.selfId() || currentIds.contains(peer.id))
            continue;
        auto* item = new QListWidgetItem(
            QStringLiteral("%1%2").arg(peerLabel(peer),
                                       peer.online ? QStringLiteral("  ●")
                                                   : QStringLiteral("  ○")),
            list);
        item->setData(Qt::UserRole, peer.id);
        item->setData(Qt::UserRole + 1, peer.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }

    if (list->count() == 0) {
        auto* empty = new QLabel(QStringLiteral(
            "Все известные пользователи уже участвуют в этом чате."), &dialog);
        empty->setWordWrap(true);
        empty->setObjectName(QStringLiteral("muted"));
        layout->addWidget(empty);
    }
    layout->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Добавить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QList<Participant> toAdd;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        Participant p;
        p.id = item->data(Qt::UserRole).toString();
        p.name = item->data(Qt::UserRole + 1).toString();
        if (!p.id.isEmpty())
            toAdd << p;
    }

    if (toAdd.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Участники"),
                                 QStringLiteral("Не выбрано ни одного пользователя."));
        return;
    }

    QJsonObject conversation;
    if (!m_rm.addParticipants(m_currentChatId, toAdd, conversation)) {
        QMessageBox::warning(this, QStringLiteral("Участники"),
                             QStringLiteral("Не удалось добавить участников:\n%1")
                                 .arg(m_rm.lastError()));
        return;
    }

    QSet<QString> targets = m_rm.participantIds(m_currentChatId);
    targets.remove(m_rm.selfId());
    if (!conversation.isEmpty())
        m_network.sendConversation(conversation, targets);

    refreshAllLists();
    statusBar()->showMessage(QStringLiteral("Участники добавлены в чат."), 4000);
}

void MainWindow::onPeerActivated(QListWidgetItem* item)
{
    Q_UNUSED(item)
    startDirectChat();
}

// =====================================================================
//  Отправка сообщений
// =====================================================================
void MainWindow::attachFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Выберите файлы"), QString(),
        QStringLiteral("Все файлы (*.*)"));
    if (files.isEmpty())
        return;

    for (const QString& file : files) {
        if (!m_pendingFiles.contains(file))
            m_pendingFiles << file;
    }
    updatePendingFilesUi();
}

void MainWindow::sendMessage()
{
    if (m_currentChatId.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Сначала выберите чат."), 4000);
        return;
    }

    const QString text = m_messageInput->text().trimmed();
    if (text.isEmpty() && m_pendingFiles.isEmpty())
        return;

    QJsonObject payload;
    Message message;
    if (!m_rm.sendLocalMessage(m_currentChatId, text, m_pendingFiles, payload, message)) {
        QMessageBox::warning(this, QStringLiteral("Отправка"),
                             QStringLiteral("Не удалось отправить сообщение:\n%1")
                                 .arg(m_rm.lastError()));
        return;
    }

    m_messageInput->clear();
    m_pendingFiles.clear();
    updatePendingFilesUi();

    if (!payload.isEmpty()) {
        QSet<QString> targets = m_rm.participantIds(m_currentChatId);
        targets.remove(m_rm.selfId());
        m_network.sendMessage(message.id, payload, targets);
    }

    const Report report = m_rm.loadOne(m_currentChatId);
    if (m_chatModel->contains(report.id))
        m_chatModel->updateFromReport(report);
    else
        refreshAllLists();

    displayActiveChat();
    updateTabsUnreadCount();
}

void MainWindow::archiveSelected()
{
    if (m_currentChatId.isEmpty())
        return;

    const QSet<QString> targets = m_rm.participantIds(m_currentChatId);
    QSet<QString> others = targets;
    others.remove(m_rm.selfId());

    if (!m_rm.archiveConversation(m_currentChatId))
        return;

    if (!others.isEmpty())
        m_network.sendArchive(m_currentChatId, others);

    m_currentChatId.clear();
    refreshAllLists();
    statusBar()->showMessage(QStringLiteral("Чат перенесён в архив."), 4000);
}

void MainWindow::deleteSelectedArchive()
{
    if (m_currentArchiveId.isEmpty())
        return;

    if (QMessageBox::question(this, QStringLiteral("Удаление"),
                              QStringLiteral("Удалить чат из архива?\n"
                                             "Чат исчезнет у всех участников."))
        != QMessageBox::Yes) {
        return;
    }

    const QString id = m_currentArchiveId;
    QSet<QString> others = m_rm.participantIds(id);
    others.remove(m_rm.selfId());

    if (!m_rm.deleteConversation(id))
        return;

    if (!others.isEmpty())
        m_network.sendDelete(id, others);

    m_currentArchiveId.clear();
    refreshAllLists();
    statusBar()->showMessage(QStringLiteral("Чат удалён."), 4000);
}

// =====================================================================
//  Обновление списков и отображение
// =====================================================================
void MainWindow::refreshAllLists()
{
    syncPeerNames();

    m_chatModel->setSelfId(m_rm.selfId());
    m_archiveModel->setSelfId(m_rm.selfId());

    const QList<Report> all = m_rm.loadAll();
    QList<Report> active;
    QList<Report> archived;
    active.reserve(all.size());

    for (const Report& r : all) {
        if (!r.isParticipant(m_rm.selfId()) && !r.participants.isEmpty())
            continue;
        if (r.status == QLatin1String("resolved"))
            archived << r;
        else
            active << r;
    }

    {
        QSignalBlocker chatLock(m_chatTable->selectionModel());
        QSignalBlocker archiveLock(m_archiveTable->selectionModel());

        m_chatModel->setReports(active);
        m_archiveModel->setReports(archived);
        m_chatProxy->sort(ReportModel::ColUpdated, Qt::DescendingOrder);
        m_archiveProxy->sort(ReportModel::ColUpdated, Qt::DescendingOrder);

        if (!m_currentChatId.isEmpty() && !m_chatModel->contains(m_currentChatId))
            m_currentChatId.clear();
        if (!m_currentArchiveId.isEmpty()
            && !m_archiveModel->contains(m_currentArchiveId)) {
            m_currentArchiveId.clear();
        }

        if (m_currentChatId.isEmpty() && m_chatModel->rowCount() > 0
            && m_tabs->currentWidget() == m_messengerTab) {
            m_currentChatId = m_chatModel->at(0).id;
        }

        restoreSelection(m_chatTable, m_chatProxy, m_chatModel, m_currentChatId);
        restoreSelection(m_archiveTable, m_archiveProxy, m_archiveModel,
                         m_currentArchiveId);
    }

    if (m_currentChatId.isEmpty())
        clearActiveChat();
    else
        displayActiveChat();

    if (m_currentArchiveId.isEmpty())
        clearArchiveChat();
    else
        displayArchiveChat();

    refreshPeerList();
    updateTabsUnreadCount();
    updateHeaderStatus();
    updateSettingsInfo();
    m_lastRevision = m_rm.revision();
}

bool MainWindow::restoreSelection(QTableView* table, QSortFilterProxyModel* proxy,
                                  ReportModel* model, const QString& reportId)
{
    if (!table || !proxy || !model || reportId.isEmpty())
        return false;

    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->at(row).id != reportId)
            continue;
        const QModelIndex proxyIndex = proxy->mapFromSource(model->index(row, 0));
        if (!proxyIndex.isValid())
            return false;
        table->setCurrentIndex(proxyIndex);
        table->selectRow(proxyIndex.row());
        return true;
    }
    return false;
}

void MainWindow::onActiveSelectionChanged()
{
    const QModelIndex current = m_chatTable->currentIndex();
    if (!current.isValid()) {
        clearActiveChat();
        return;
    }

    const QModelIndex source = m_chatProxy->mapToSource(current);
    const Report report = m_chatModel->at(source.row());
    if (report.id.isEmpty()) {
        clearActiveChat();
        return;
    }

    if (m_currentChatId != report.id) {
        m_pendingFiles.clear();
        updatePendingFilesUi();
    }
    m_currentChatId = report.id;
    displayActiveChat();
}

void MainWindow::onArchiveSelectionChanged()
{
    const QModelIndex current = m_archiveTable->currentIndex();
    if (!current.isValid()) {
        clearArchiveChat();
        return;
    }

    const QModelIndex source = m_archiveProxy->mapToSource(current);
    const Report report = m_archiveModel->at(source.row());
    if (report.id.isEmpty()) {
        clearArchiveChat();
        return;
    }

    m_currentArchiveId = report.id;
    displayArchiveChat();
}

void MainWindow::displayActiveChat()
{
    if (m_currentChatId.isEmpty()) {
        clearActiveChat();
        return;
    }

    Report report = m_rm.loadOne(m_currentChatId);
    if (report.id.isEmpty()) {
        clearActiveChat();
        return;
    }

    const bool direct = report.kind == QLatin1String("direct");
    if (m_addMembersButton)
        m_addMembersButton->setVisible(!direct);
    QString meta;
    if (direct) {
        QString peerId;
        for (const Participant& p : report.participants) {
            if (p.id != m_rm.selfId()) {
                peerId = p.id;
                break;
            }
        }
        bool online = false;
        for (const PeerInfo& peer : m_network.peers()) {
            if (peer.id == peerId) {
                online = peer.online;
                break;
            }
        }
        meta = online ? QStringLiteral("● в сети") : QStringLiteral("○ не в сети");
    } else {
        const QString summary = report.participantSummary(m_rm.selfId());
        meta = summary.isEmpty() ? QStringLiteral("Групповой чат")
                                 : QStringLiteral("Участники: %1").arg(summary);
    }

    m_chatDetail->setText(
        QStringLiteral("<b>%1</b><br><span style='color:#64748b;'>%2 · %3</span>")
            .arg(report.displayTitle(m_rm.selfId()).toHtmlEscaped(),
                 (direct ? QStringLiteral("Личный чат") : QStringLiteral("Групповой чат")),
                 meta.toHtmlEscaped()));

    renderAttachments(m_chatAttachLayout, report, report.attachments, false);
    rebuildChat(m_chatLayout, m_chatScroll, report, false);

    if (m_tabs && m_tabs->currentWidget() == m_messengerTab
        && m_rm.markConversationRead(report.id)) {
        report = m_rm.loadOne(report.id);
        if (m_chatModel->contains(report.id))
            m_chatModel->updateFromReport(report);
    }

    updateActiveActions();
    updateTabsUnreadCount();
}

void MainWindow::displayArchiveChat()
{
    if (m_currentArchiveId.isEmpty()) {
        clearArchiveChat();
        return;
    }

    const Report report = m_rm.loadOne(m_currentArchiveId);
    if (report.id.isEmpty()) {
        clearArchiveChat();
        return;
    }

    m_archiveDetail->setText(
        QStringLiteral("<b>%1</b><br><span style='color:#64748b;'>%2 · архив</span>")
            .arg(report.displayTitle(m_rm.selfId()).toHtmlEscaped(),
                 report.participantSummary(m_rm.selfId()).toHtmlEscaped()));

    renderAttachments(m_archiveAttachLayout, report, report.attachments, true);
    rebuildChat(m_archiveLayout, m_archiveScroll, report, true);
    updateArchiveActions();
}

void MainWindow::clearActiveChat()
{
    m_currentChatId.clear();
    if (m_chatDetail)
        m_chatDetail->setText(QStringLiteral("Выберите чат слева или начните переписку."));
    if (m_chatAttachWidget)
        m_chatAttachWidget->setVisible(false);
    if (m_addMembersButton)
        m_addMembersButton->setVisible(false);
    if (m_chatLayout) {
        clearChatLayout(m_chatLayout);
        showChatPlaceholder(m_chatLayout, QStringLiteral("Сообщений пока нет."));
    }
    updateActiveActions();
}

void MainWindow::clearArchiveChat()
{
    m_currentArchiveId.clear();
    if (m_archiveDetail)
        m_archiveDetail->setText(QStringLiteral("Выберите чат в архиве."));
    if (m_archiveAttachWidget)
        m_archiveAttachWidget->setVisible(false);
    if (m_archiveLayout) {
        clearChatLayout(m_archiveLayout);
        showChatPlaceholder(m_archiveLayout, QStringLiteral("Сообщений нет."));
    }
    updateArchiveActions();
}

void MainWindow::clearChatLayout(QVBoxLayout* layout)
{
    if (!layout)
        return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void MainWindow::showChatPlaceholder(QVBoxLayout* layout, const QString& text)
{
    if (!layout)
        return;
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("muted"));
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
}

void MainWindow::rebuildChat(QVBoxLayout* chatLayout, QScrollArea* scroll,
                             const Report& report, bool readOnly)
{
    clearChatLayout(chatLayout);
    if (report.messages.isEmpty()) {
        showChatPlaceholder(chatLayout, QStringLiteral("Сообщений пока нет."));
        return;
    }

    for (const Message& message : report.messages)
        appendChatBubble(chatLayout, scroll, report, message, readOnly);

    chatLayout->addStretch(1);
    scrollChatToBottom(scroll);
}

void MainWindow::appendChatBubble(QVBoxLayout* chatLayout, QScrollArea* scroll,
                                  const Report& report, const Message& message,
                                  bool readOnly)
{
    Q_UNUSED(scroll)
    if (!chatLayout)
        return;

    const bool own = !message.senderId.isEmpty() && message.senderId == m_rm.selfId();

    auto* row = new QWidget;
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    auto* bubble = new QFrame;
    bubble->setObjectName(own ? QStringLiteral("bubbleOwn") : QStringLiteral("bubbleOther"));
    bubble->setMaximumWidth(640);
    auto* bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(10, 8, 10, 8);
    bubbleLayout->setSpacing(4);

    const QString sender = message.senderName.trimmed().isEmpty()
                               ? message.senderId.left(8)
                               : message.senderName;
    auto* meta = new QLabel(QStringLiteral("<b>%1</b> <span style='color:#64748b;'>%2</span>")
                                .arg(sender.toHtmlEscaped(), message.ts.toHtmlEscaped()));
    meta->setTextFormat(Qt::RichText);
    bubbleLayout->addWidget(meta);

    if (!message.text.trimmed().isEmpty()) {
        auto* textLabel = new QLabel(message.text.toHtmlEscaped());
        textLabel->setWordWrap(true);
        textLabel->setTextFormat(Qt::RichText);
        textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bubbleLayout->addWidget(textLabel);
    }

    if (!message.attachments.isEmpty()) {
        auto* attachRow = new QWidget;
        auto* attachLayout = new QHBoxLayout(attachRow);
        attachLayout->setContentsMargins(0, 2, 0, 0);
        attachLayout->setSpacing(6);
        renderAttachments(attachLayout, report, message.attachments, readOnly);
        bubbleLayout->addWidget(attachRow);
    }

    if (own)
        rowLayout->addStretch(1);
    rowLayout->addWidget(bubble, 0);
    if (!own)
        rowLayout->addStretch(1);

    chatLayout->addWidget(row);
}

void MainWindow::scrollChatToBottom(QScrollArea* scroll)
{
    if (!scroll)
        return;
    QTimer::singleShot(0, scroll, [scroll] {
        if (auto* bar = scroll->verticalScrollBar())
            bar->setValue(bar->maximum());
    });
}

void MainWindow::renderAttachments(QHBoxLayout* layout, const Report& report,
                                   const QList<Attachment>& attachments,
                                   bool readOnly)
{
    if (!layout)
        return;

    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    if (attachments.isEmpty()) {
        if (layout->parentWidget())
            layout->parentWidget()->setVisible(false);
        return;
    }

    for (const Attachment& attachment : attachments)
        layout->addWidget(createAttachmentChip(report, attachment, readOnly));

    layout->addStretch(1);
    if (layout->parentWidget())
        layout->parentWidget()->setVisible(true);
}

QWidget* MainWindow::createAttachmentChip(const Report& report, const Attachment& attachment,
                                          bool readOnly)
{
    const bool exists = QFileInfo::exists(m_rm.getAttachmentPath(attachment.filename));
    QString label = attachment.visibleName();
    if (!exists)
        label += QStringLiteral("  ⬇");
    if (attachment.size > 0)
        label += QStringLiteral(" (%1)").arg(formatSize(attachment.size));

    auto* button = new QPushButton(label);
    button->setObjectName(QStringLiteral("btnSecondary"));
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(exists
                           ? QStringLiteral("Открыть файл")
                           : QStringLiteral("Скачать файл у отправителя"));

    connect(button, &QPushButton::clicked, this, [this, report, attachment] {
        handleAttachmentAction(report, attachment);
    });
    Q_UNUSED(readOnly)
    return button;
}

void MainWindow::handleAttachmentAction(const Report& report, const Attachment& attachment)
{
    const QString path = m_rm.getAttachmentPath(attachment.filename);
    if (QFileInfo::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }

    PeerInfo owner;
    bool found = false;
    for (const PeerInfo& peer : m_network.peers()) {
        if (!attachment.ownerId.isEmpty() && peer.id == attachment.ownerId) {
            owner = peer;
            found = true;
            break;
        }
        if (attachment.ownerId.isEmpty() && !attachment.ownerName.isEmpty()
            && peer.name == attachment.ownerName) {
            owner = peer;
            found = true;
            break;
        }
    }

    if (!found || !owner.online) {
        const QString who = attachment.ownerName.isEmpty()
                                ? (attachment.ownerId.isEmpty()
                                       ? QStringLiteral("отправитель")
                                       : attachment.ownerId.left(8))
                                : attachment.ownerName;
        QMessageBox::information(
            this, QStringLiteral("Файл недоступен"),
            QStringLiteral("Файл ещё не скачан, а %1 сейчас не в сети.\n"
                           "Повторите попытку, когда пользователь появится в сети.")
                .arg(who));
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    statusBar()->showMessage(
        QStringLiteral("Скачивание файла: %1…").arg(attachment.filename), 5000);
    m_network.downloadFromPeer(owner, attachment.filename, attachment.size, path);
}

void MainWindow::onDownloadFinished(const QString& filename, const QString& path,
                                    bool ok, const QString& error)
{
    Q_UNUSED(path)
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Загрузка файла"),
                             QStringLiteral("Не удалось скачать «%1»:\n%2")
                                 .arg(filename,
                                      error.isEmpty() ? QStringLiteral("неизвестная ошибка")
                                                      : error));
        return;
    }

    m_rm.markAttachmentDownloaded(filename);
    statusBar()->showMessage(QStringLiteral("Файл скачан: %1").arg(filename), 5000);
    refreshAllLists();
}

// =====================================================================
//  Интерфейс: статусы и действия
// =====================================================================
void MainWindow::updateTabsUnreadCount()
{
    const int unread = m_rm.unreadIncomingCount();
    const QString title = unread > 0
                              ? QStringLiteral("Мессенджер (%1)").arg(unread)
                              : QStringLiteral("Мессенджер");
    const int index = m_tabs->indexOf(m_messengerTab);
    if (index >= 0)
        m_tabs->setTabText(index, title);
}

void MainWindow::updateHeaderStatus()
{
    const SettingsManager& settings = SettingsManager::instance();
    m_headerPcLabel->setText(
        QStringLiteral("Пользователь: %1").arg(settings.pcName()));
    m_headerStatusLabel->setText(
        QStringLiteral("В сети: %1").arg(m_network.onlineCount() + 1));
    m_dbStatusLabel->setText(QStringLiteral("Чатов: %1").arg(m_rm.totalCount()));
}

void MainWindow::updateSettingsInfo()
{
    if (m_reportsDbPathEdit)
        m_reportsDbPathEdit->setText(m_rm.dbPath());
    if (m_devicesDbPathEdit)
        m_devicesDbPathEdit->setText(SettingsManager::instance().devicesDbPath());
    if (m_settingsStatsLabel) {
        m_settingsStatsLabel->setText(
            QStringLiteral("Активных чатов: %1 · В архиве: %2 · Новых сообщений: %3")
                .arg(m_rm.totalCount() - m_rm.resolvedCount())
                .arg(m_rm.resolvedCount())
                .arg(m_rm.unreadIncomingCount()));
    }

    if (m_settingsMdbStatusLabel) {
        const QString mdbPath = SettingsManager::instance().mdbPath();
        if (mdbPath.trimmed().isEmpty()) {
            m_settingsMdbStatusLabel->setText(QStringLiteral("Файл MDB не выбран."));
        } else if (!QFileInfo::exists(mdbPath)) {
            m_settingsMdbStatusLabel->setText(
                QStringLiteral("Файл MDB не найден: %1").arg(mdbPath));
        } else {
            m_settingsMdbStatusLabel->setText(QStringLiteral(
                "Файл MDB доступен. Приборы читаются из локального кэша; "
                "для обновления используйте «Обновить из MDB» на вкладке «Приборы»."));
        }
    }
}

void MainWindow::updatePendingFilesUi()
{
    if (!m_pendingFilesLabel)
        return;

    if (m_pendingFiles.isEmpty()) {
        m_pendingFilesLabel->clear();
        m_pendingFilesLabel->setVisible(false);
        if (m_clearFilesButton)
            m_clearFilesButton->setVisible(false);
        return;
    }

    QStringList names;
    for (const QString& file : m_pendingFiles)
        names << QFileInfo(file).fileName();
    m_pendingFilesLabel->setText(
        QStringLiteral("Вложения (%1): %2")
            .arg(m_pendingFiles.size())
            .arg(names.join(QStringLiteral(", "))));
    m_pendingFilesLabel->setToolTip(names.join(QLatin1Char('\n')));
    m_pendingFilesLabel->setVisible(true);
    if (m_clearFilesButton)
        m_clearFilesButton->setVisible(true);
}

void MainWindow::updateActiveActions()
{
    const bool hasChat = !m_currentChatId.isEmpty();
    if (m_messageInput)
        m_messageInput->setEnabled(hasChat);
    if (m_sendButton)
        m_sendButton->setEnabled(hasChat);
    if (m_attachButton)
        m_attachButton->setEnabled(hasChat);
    if (m_clearFilesButton)
        m_clearFilesButton->setEnabled(hasChat);
    if (m_archiveButton)
        m_archiveButton->setEnabled(hasChat);
}

void MainWindow::updateArchiveActions()
{
    if (m_archiveDeleteButton)
        m_archiveDeleteButton->setEnabled(!m_currentArchiveId.isEmpty());
}

void MainWindow::syncPeerNames()
{
    for (const PeerInfo& peer : m_network.peers()) {
        if (peer.id.isEmpty() || peer.name.trimmed().isEmpty())
            continue;
        m_rm.updateParticipantName(peer.id, peer.name);
    }
}

// =====================================================================
//  Настройки
// =====================================================================
void MainWindow::saveSettings()
{
    SettingsManager& settings = SettingsManager::instance();
    const QString oldPath = settings.reportsPath();
    const QString oldName = settings.pcName();
    const int oldPort = settings.networkPort();

    settings.setReportsPath(m_pathEdit->text().trimmed());
    settings.setPcName(m_nameEdit->text().trimmed());
    settings.setMdbPath(m_mdbPathEdit->text().trimmed());
    settings.setNetworkPort(m_portSpin->value());

    if (oldPath != settings.reportsPath()) {
        m_rm.setBasePath(settings.reportsPath());
        m_currentChatId.clear();
        m_currentArchiveId.clear();
        m_lastRevision = -1;
    }

    if (oldName != settings.pcName()) {
        m_network.updateSelfName(settings.pcName());
        m_rm.setIdentity(m_rm.selfId(), settings.pcName());
        m_rm.updateParticipantName(m_rm.selfId(), settings.pcName());
    }

    if (oldPort != settings.networkPort()) {
        m_network.stop();
        maybeStartNetwork();
    }

    if (m_devicesWidget)
        m_devicesWidget->onSettingsChanged();

    refreshAllLists();
    updateHeaderStatus();
    statusBar()->showMessage(QStringLiteral("Настройки сохранены."), 4000);
}

// =====================================================================
//  Сеть: изменения ростера и входящие данные
// =====================================================================
void MainWindow::onRosterChanged()
{
    const qint64 before = m_rm.revision();
    syncPeerNames();
    if (m_rm.revision() != before)
        refreshAllLists();
    else {
        refreshPeerList();
        updateHeaderStatus();
        if (!m_currentChatId.isEmpty())
            displayActiveChat();
    }
}

void MainWindow::onRemoteMessage(const QJsonObject& payload)
{
    bool isNew = false;
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("conversation")) {
        m_rm.importConversation(payload.value(QStringLiteral("conversation")).toObject(),
                                isNew);
    } else {
        m_rm.importNetworkMessage(payload, isNew);
    }

    if (isNew || m_rm.revision() != m_lastRevision)
        refreshAllLists();

    if (isNew)
        statusBar()->showMessage(QStringLiteral("Получены новые сообщения."), 4000);
}

void MainWindow::onHistoryReceived(const QJsonArray& items)
{
    bool changed = false;
    for (const QJsonValue& value : items) {
        bool isNew = false;
        if (m_rm.importConversation(value.toObject(), isNew) && isNew)
            changed = true;
    }
    if (changed || m_rm.revision() != m_lastRevision)
        refreshAllLists();
}

void MainWindow::onRemoteArchive(const QString& id)
{
    if (id.isEmpty())
        return;
    m_rm.archiveConversation(id);
    if (m_currentChatId == id)
        m_currentChatId.clear();
    refreshAllLists();
}

void MainWindow::onRemoteDelete(const QString& id)
{
    if (id.isEmpty())
        return;
    m_rm.deleteConversation(id);
    if (m_currentArchiveId == id)
        m_currentArchiveId.clear();
    if (m_currentChatId == id)
        m_currentChatId.clear();
    refreshAllLists();
}

void MainWindow::autoRefresh()
{
    if (m_rm.revision() != m_lastRevision) {
        refreshAllLists();
    } else {
        updateHeaderStatus();
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    return QMainWindow::eventFilter(obj, event);
}
