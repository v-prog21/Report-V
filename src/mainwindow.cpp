#include "mainwindow.h"
#include "settingsmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QClipboard>
#include <QMimeData>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QDateTime>
#include <QScrollBar>
#include <QDialog>
#include <QPixmap>
#include <QApplication>
#include <QSettings>
#include <QGraphicsDropShadowEffect>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_rm(SettingsManager::instance().reportsPath()) {
    setWindowTitle("Support IL");
    resize(1400, 900);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    
    m_tabs->addTab(buildUserTab(),     "Мой отчёт");
    m_adminTabWidget = buildAdminTab();
    m_tabs->addTab(m_adminTabWidget,   "Админ");
    m_tabs->addTab(buildArchiveTab(),  "Архив");
    m_tabs->addTab(buildRepoTab(),     "Репозитории");
    m_tabs->addTab(buildSettingsTab(), "Настройки");
    setCentralWidget(m_tabs);

    // Инициализация видимости вкладки Админ
    updateAdminTabVisibility();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::autoRefresh);
    m_refreshTimer->start(2000);

    refreshAllLists();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::Paste) && obj == m_userDescEdit) {
            const QClipboard* clip = QApplication::clipboard();
            if (clip->mimeData()->hasImage()) {
                QImage img = qvariant_cast<QImage>(clip->mimeData()->imageData());
                if (!img.isNull()) {
                    QString tmp = QDir::temp().filePath(QString("bug_%1.png").arg(QDateTime::currentMSecsSinceEpoch()));
                    img.save(tmp, "PNG");
                    m_pendingImages << tmp;
                    m_userDescEdit->insertPlainText(QString("\n[Скриншот прикреплен]\n"));
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ---------- USER TAB ----------
QWidget* MainWindow::buildUserTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setContentsMargins(15, 15, 15, 15);
    root->setSpacing(15);

    auto* form = new QGroupBox("Создать новый отчёт");
    auto* fl = new QVBoxLayout(form);
    m_userTitleEdit = new QLineEdit;
    m_userTitleEdit->setPlaceholderText("Краткое название проблемы...");
    m_userTitleEdit->setMinimumHeight(40);
    
    m_userDescEdit = new QTextEdit;
    m_userDescEdit->setPlaceholderText("Подробно опишите, что произошло... (Ctrl+V для вставки скрина)");
    m_userDescEdit->setMaximumHeight(100);
    m_userDescEdit->installEventFilter(this);
    
    fl->addWidget(m_userTitleEdit);
    fl->addWidget(m_userDescEdit);

    auto* btnRow = new QHBoxLayout;
    auto* createBtn = new QPushButton("Отправить отчёт");
    btnRow->addStretch();
    btnRow->addWidget(createBtn);
    fl->addLayout(btnRow);
    root->addWidget(form);

    auto* splitter = new QSplitter(Qt::Horizontal);
    
    auto* listBox = new QGroupBox("Мои обращения");
    auto* ll = new QVBoxLayout(listBox);
    auto* search = new QLineEdit;
    search->setPlaceholderText("Поиск...");
    search->setMinimumHeight(35);
    ll->addWidget(search);
    
    m_userModel = new ReportModel(this);
    m_userProxy = new QSortFilterProxyModel(this);
    m_userProxy->setSourceModel(m_userModel);
    m_userProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_userProxy->setFilterKeyColumn(-1);
    
    m_userTable = new QTableView;
    m_userTable->setModel(m_userProxy);
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->verticalHeader()->setVisible(false);
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->setColumnHidden(0, true);
    m_userTable->setShowGrid(false);
    ll->addWidget(m_userTable);
    splitter->addWidget(listBox);
    
    auto* chatBox = new QGroupBox("Переписка");
    auto* cbl = new QVBoxLayout(chatBox);
    
    m_userDetail = new QLabel("Выберите отчёт для просмотра.");
    m_userDetail->setWordWrap(true);
    m_userDetail->setStyleSheet("font-size: 11pt; color: #334155;");
    cbl->addWidget(m_userDetail);

    m_userAttachWidget = new QWidget;
    m_userAttachLayout = new QHBoxLayout(m_userAttachWidget);
    m_userAttachLayout->setContentsMargins(0,0,0,0);
    m_userAttachLayout->setAlignment(Qt::AlignLeft);
    cbl->addWidget(m_userAttachWidget);
    
    m_userChatScroll = new QScrollArea;
    m_userChatScroll->setWidgetResizable(true);
    m_userChatScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_userChatWidget = new QWidget;
    m_userChatLayout = new QVBoxLayout(m_userChatWidget);
    m_userChatLayout->setAlignment(Qt::AlignTop);
    m_userChatScroll->setWidget(m_userChatWidget);
    cbl->addWidget(m_userChatScroll, 1);

    auto* inputRow = new QHBoxLayout;
    m_userMsgInput = new QLineEdit;
    m_userMsgInput->setPlaceholderText("Уточнить детали...");
    m_userMsgInput->setMinimumHeight(40);
    auto* userSendBtn = new QPushButton("Отправить");
    userSendBtn->setMinimumHeight(40);
    inputRow->addWidget(m_userMsgInput, 1);
    inputRow->addWidget(userSendBtn);
    cbl->addLayout(inputRow);

    splitter->addWidget(chatBox);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    connect(createBtn, &QPushButton::clicked, this, &MainWindow::createReport);
    connect(search, &QLineEdit::textChanged, [this](const QString& t){ m_userProxy->setFilterFixedString(t); });
    connect(m_userTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onUserSelectionChanged);
    connect(userSendBtn, &QPushButton::clicked, this, &MainWindow::sendUserMessage);
    connect(m_userMsgInput, &QLineEdit::returnPressed, this, &MainWindow::sendUserMessage);

    return w;
}

void MainWindow::onUserSelectionChanged() {
    auto idx = m_userTable->currentIndex();
    if (!idx.isValid()) return;
    auto src = m_userProxy->mapToSource(idx);
    Report r = m_userModel->at(src.row());
    m_currentUserId = r.id;

    m_userDetail->setText(QString("<b>%1</b><br><span style='color:#64748b; font-size: 9pt;'>Создан: %2</span><br><br>%3")
        .arg(r.title, r.created, r.description));

    renderAttachments(m_userAttachLayout, r);

    QLayoutItem* child;
    while ((child = m_userChatLayout->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget(); 
        delete child;
    }

    buildChatUI(m_userChatWidget, m_userChatLayout, r, false);
    QScrollBar* sb = m_userChatScroll->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::sendUserMessage() {
    if (m_currentUserId.isEmpty()) return;
    QString text = m_userMsgInput->text().trimmed();
    if (text.isEmpty()) return;
    
    m_rm.addMessage(m_currentUserId, text, "user");
    m_userMsgInput->clear();
    
    // Мгновенное обновление текущего чата
    m_currentReport = m_rm.loadOne(m_currentUserId);
    onUserSelectionChanged();
    
    refreshAllLists();
}

void MainWindow::createReport() {
    QString title = m_userTitleEdit->text().trimmed();
    QString desc  = m_userDescEdit->toPlainText().trimmed();
    if (title.isEmpty() && desc.isEmpty()) return;
    m_rm.createReport(title, desc, m_pendingImages);
    m_userTitleEdit->clear();
    m_userDescEdit->clear();
    m_pendingImages.clear();
    refreshAllLists();
}

// ---------- ADMIN TAB ----------
QWidget* MainWindow::buildAdminTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setContentsMargins(15, 15, 15, 15);
    root->setSpacing(15);
    
    auto* splitter = new QSplitter(Qt::Horizontal);
    
    auto* listBox = new QGroupBox("Входящие заявки");
    auto* ll = new QVBoxLayout(listBox);
    auto* search = new QLineEdit;
    search->setPlaceholderText("Поиск...");
    search->setMinimumHeight(35);
    ll->addWidget(search);

    m_adminModel = new ReportModel(this);
    m_adminProxy = new QSortFilterProxyModel(this);
    m_adminProxy->setSourceModel(m_adminModel);
    m_adminProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_adminProxy->setFilterKeyColumn(-1);
    
    m_adminTable = new QTableView;
    m_adminTable->setModel(m_adminProxy);
    m_adminTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_adminTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_adminTable->verticalHeader()->setVisible(false);
    m_adminTable->horizontalHeader()->setStretchLastSection(true);
    m_adminTable->setShowGrid(false);
    ll->addWidget(m_adminTable);
    splitter->addWidget(listBox);

    auto* chatBox = new QGroupBox("Диалог с пользователем");
    auto* cbl = new QVBoxLayout(chatBox);
    m_adminDetail = new QLabel("Выберите тикет.");
    m_adminDetail->setWordWrap(true);
    m_adminDetail->setStyleSheet("font-size: 11pt; color: #334155;");
    cbl->addWidget(m_adminDetail);

    m_adminAttachWidget = new QWidget;
    m_adminAttachLayout = new QHBoxLayout(m_adminAttachWidget);
    m_adminAttachLayout->setContentsMargins(0,0,0,0);
    m_adminAttachLayout->setAlignment(Qt::AlignLeft);
    cbl->addWidget(m_adminAttachWidget);

    m_adminChatScroll = new QScrollArea;
    m_adminChatScroll->setWidgetResizable(true);
    m_adminChatScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_adminChatWidget = new QWidget;
    m_adminChatLayout = new QVBoxLayout(m_adminChatWidget);
    m_adminChatLayout->setAlignment(Qt::AlignTop);
    m_adminChatScroll->setWidget(m_adminChatWidget);
    cbl->addWidget(m_adminChatScroll, 1);

    auto* inputRow = new QHBoxLayout;
    m_adminMsgInput = new QLineEdit;
    m_adminMsgInput->setPlaceholderText("Написать пользователю...");
    m_adminMsgInput->setMinimumHeight(40);
    auto* sendBtn = new QPushButton("Ответить");
    sendBtn->setMinimumHeight(40);
    auto* resolveBtn = new QPushButton("Решено");
    resolveBtn->setStyleSheet("background-color: #10b981; color: white;");
    resolveBtn->setMinimumHeight(40);
    
    inputRow->addWidget(m_adminMsgInput, 1);
    inputRow->addWidget(sendBtn);
    inputRow->addWidget(resolveBtn);
    cbl->addLayout(inputRow);

    splitter->addWidget(chatBox);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    connect(search, &QLineEdit::textChanged, [this](const QString& t){ m_adminProxy->setFilterFixedString(t); });
    connect(m_adminTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onAdminSelectionChanged);
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::sendAdminMessage);
    connect(m_adminMsgInput, &QLineEdit::returnPressed, this, &MainWindow::sendAdminMessage);
    connect(resolveBtn, &QPushButton::clicked, this, &MainWindow::resolveSelected);
    return w;
}

void MainWindow::onAdminSelectionChanged() {
    auto idx = m_adminTable->currentIndex();
    if (!idx.isValid()) return;
    auto src = m_adminProxy->mapToSource(idx);
    Report r = m_adminModel->at(src.row());
    m_currentAdminId = r.id;

    m_adminDetail->setText(QString("<b>%1</b><br><span style='color:#64748b; font-size: 9pt;'>От: %2 | %3</span><br><br>%4")
        .arg(r.title, r.author, r.created, r.description));

    renderAttachments(m_adminAttachLayout, r);

    QLayoutItem* child;
    while ((child = m_adminChatLayout->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget();
        delete child;
    }

    buildChatUI(m_adminChatWidget, m_adminChatLayout, r, true);
    QScrollBar* sb = m_adminChatScroll->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::sendAdminMessage() {
    if (m_currentAdminId.isEmpty()) return;
    QString text = m_adminMsgInput->text().trimmed();
    if (text.isEmpty()) return;
    
    m_rm.addMessage(m_currentAdminId, text, "admin");
    m_adminMsgInput->clear();
    
    // Мгновенное обновление
    m_currentReport = m_rm.loadOne(m_currentAdminId);
    onAdminSelectionChanged();
    
    refreshAllLists();
}

void MainWindow::resolveSelected() {
    if (m_currentAdminId.isEmpty()) return;
    if (QMessageBox::question(this, "Подтверждение", "Переместить в архив?") == QMessageBox::Yes) {
        m_rm.resolve(m_currentAdminId);
        m_currentAdminId.clear();
        
        QLayoutItem* child;
        while ((child = m_adminChatLayout->takeAt(0)) != nullptr) { if (child->widget()) delete child->widget(); delete child; }
        while ((child = m_adminAttachLayout->takeAt(0)) != nullptr) { if (child->widget()) delete child->widget(); delete child; }
        
        m_adminDetail->setText("Отчёт перемещен в архив.");
        refreshAllLists();
    }
}

// ---------- MESSENGER UI ----------
void MainWindow::buildChatUI(QWidget* parentWidget, QVBoxLayout* chatLayout, const Report& r, bool isAdminView) {
    for (const auto& m : r.messages) {
        auto* msgRow = new QWidget;
        auto* rowLayout = new QHBoxLayout(msgRow);
        rowLayout->setContentsMargins(0, 5, 0, 5);

        auto* bubble = new QLabel;
        bubble->setWordWrap(true);
        bubble->setTextFormat(Qt::RichText);
        bubble->setMaximumWidth(600);
        
        QString header = QString("<span style='font-size:8pt; color:#94a3b8;'>%1</span><br>").arg(m.ts);
        bubble->setText(header + m.text.toHtmlEscaped());

        auto* shadow = new QGraphicsDropShadowEffect(bubble);
        shadow->setBlurRadius(10);
        shadow->setColor(QColor(0, 0, 0, 30));
        shadow->setOffset(0, 2);
        bubble->setGraphicsEffect(shadow);

        if (m.role == "admin") {
            bubble->setStyleSheet("background-color: #eff6ff; color: #1e40af; border-radius: 12px; padding: 12px; border: 1px solid #dbeafe;");
            rowLayout->addStretch();
            rowLayout->addWidget(bubble);
        } else {
            bubble->setStyleSheet("background-color: #ffffff; color: #334155; border-radius: 12px; padding: 12px; border: 1px solid #e2e8f0;");
            rowLayout->addWidget(bubble);
            rowLayout->addStretch();
        }
        chatLayout->addWidget(msgRow);
    }
}

void MainWindow::renderAttachments(QHBoxLayout* layout, const Report& r) {
    QLayoutItem* child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget(); 
        delete child;
    }

    if (r.attachments.isEmpty()) return;

    for (const auto& att : r.attachments) {
        QPixmap pix(m_rm.getAttachmentPath(att));
        if (!pix.isNull()) {
            auto* btn = new QPushButton;
            btn->setIcon(QIcon(pix.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            btn->setIconSize(QSize(80, 80));
            btn->setFixedSize(90, 90);
            btn->setStyleSheet("background: white; border: 1px solid #cbd5e1; border-radius: 8px;");
            btn->setToolTip("Открыть в полном размере");
            QString fullPath = m_rm.getAttachmentPath(att);
            connect(btn, &QPushButton::clicked, this, [this, fullPath](){ openAttachment(fullPath); });
            layout->addWidget(btn);
        }
    }
    layout->addStretch();
}

void MainWindow::openAttachment(const QString& path) {
    QDialog dlg(this);
    dlg.setWindowTitle("Просмотр изображения");
    dlg.resize(1100, 850);
    
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(0,0,0,0);
    
    auto* scroll = new QScrollArea;
    auto* lbl = new QLabel;
    QPixmap pix(path);
    if (pix.isNull()) return;
    lbl->setPixmap(pix);
    scroll->setWidget(lbl);
    scroll->setAlignment(Qt::AlignCenter);
    layout->addWidget(scroll);
    
    dlg.exec();
}

// ---------- ARCHIVE TAB ----------
QWidget* MainWindow::buildArchiveTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setContentsMargins(15, 15, 15, 15);
    
    auto* search = new QLineEdit;
    search->setPlaceholderText("Поиск в архиве...");
    search->setMinimumHeight(35);
    root->addWidget(search);

    m_archiveModel = new ReportModel(this);
    m_archiveProxy = new QSortFilterProxyModel(this);
    m_archiveProxy->setSourceModel(m_archiveModel);
    m_archiveProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_archiveProxy->setFilterKeyColumn(-1);
    
    m_archiveTable = new QTableView;
    m_archiveTable->setModel(m_archiveProxy);
    m_archiveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_archiveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_archiveTable->verticalHeader()->setVisible(false);
    m_archiveTable->setShowGrid(false);
    root->addWidget(m_archiveTable, 1);

    connect(search, &QLineEdit::textChanged, [this](const QString& t){ m_archiveProxy->setFilterFixedString(t); });
    connect(m_archiveTable, &QTableView::doubleClicked, this, &MainWindow::onArchiveDoubleClick);
    
    return w;
}

void MainWindow::onArchiveDoubleClick(const QModelIndex& index) {
    if (!index.isValid()) return;
    auto src = m_archiveProxy->mapToSource(index);
    Report r = m_archiveModel->at(src.row());
    showArchiveReader(r);
}

void MainWindow::showArchiveReader(const Report& r) {
    QDialog dlg(this);
    dlg.setWindowTitle("Архивный отчёт: " + r.title);
    dlg.resize(800, 600);
    
    auto* layout = new QVBoxLayout(&dlg);
    
    auto* header = new QLabel(QString("<h2>%1</h2><p style='color:#64748b;'>Автор: %2 | Дата: %3</p><p>%4</p>")
        .arg(r.title, r.author, r.created, r.description));
    header->setWordWrap(true);
    layout->addWidget(header);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    auto* chatWidget = new QWidget;
    auto* chatLayout = new QVBoxLayout(chatWidget);
    chatLayout->setAlignment(Qt::AlignTop);
    
    for (const auto& m : r.messages) {
        auto* bubble = new QLabel;
        bubble->setWordWrap(true);
        bubble->setTextFormat(Qt::RichText);
        bubble->setText(QString("<span style='font-size:8pt; color:#94a3b8;'>%1 [%2]</span><br>%3")
            .arg(m.author, m.ts, m.text.toHtmlEscaped()));
        bubble->setStyleSheet("background-color: #f1f5f9; color: #334155; border-radius: 8px; padding: 10px; margin-bottom: 5px;");
        chatLayout->addWidget(bubble);
    }
    scroll->setWidget(chatWidget);
    layout->addWidget(scroll, 1);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    btnBox->button(QDialogButtonBox::Close)->setText("Закрыть");
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btnBox);

    dlg.exec();
}

// ---------- REPOSITORIES TAB ----------
QWidget* MainWindow::buildRepoTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setContentsMargins(15, 15, 15, 15);

    auto* list = new QListWidget;
    list->setStyleSheet("QListWidget::item { padding: 12px; border-bottom: 1px solid #e2e8f0; font-size: 11pt; }");
    
    auto* add = new QPushButton("Добавить ссылку");
    add->setObjectName("btnSecondary");
    auto* row = new QHBoxLayout;
    row->addWidget(add); row->addStretch();
    root->addWidget(new QLabel("Двойной клик по элементу открывает папку или документ:"));
    root->addWidget(list, 1);
    root->addLayout(row);

    QString repoPath = SettingsManager::instance().reportsPath() + "/repositories.json";
    QFile file(repoPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonArray arr = doc.array();
        for (auto val : arr) list->addItem(val.toString());
        file.close();
    }

    connect(add, &QPushButton::clicked, [this, list, repoPath]{
        QString p = QFileDialog::getExistingDirectory(this, "Выберите папку");
        if (p.isEmpty()) {
            p = QFileDialog::getOpenFileName(this, "Выберите документ", QString(), "Документы (*.doc *.docx *.pdf *.txt);;Все файлы (*.*)");
        }
        if (p.isEmpty()) return;
        list->addItem(p);
        
        QJsonArray arr;
        for (int i = 0; i < list->count(); ++i) arr.append(list->item(i)->text());
        QFile outFile(repoPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(QJsonDocument(arr).toJson());
            outFile.close();
        }
    });
    
    connect(list, &QListWidget::itemDoubleClicked, [](QListWidgetItem* item){
        if (item) QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
    });
    
    return w;
}

// ---------- SETTINGS TAB ----------
QWidget* MainWindow::buildSettingsTab() {
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setContentsMargins(15, 15, 15, 15);
    
    auto* box = new QGroupBox("Основные настройки");
    auto* fl = new QFormLayout(box);

    auto& st = SettingsManager::instance();
    m_pathEdit = new QLineEdit(st.reportsPath());
    auto* browse = new QPushButton("Обзор...");
    browse->setObjectName("btnSecondary");
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(browse);

    m_pcEdit = new QLineEdit(st.pcName());
    m_showAllChk = new QCheckBox("Включить режим администратора");
    m_showAllChk->setChecked(st.showAllReports());

    auto* save = new QPushButton("Сохранить настройки");
    save->setMinimumHeight(40);

    fl->addRow("Папка отчётов:", pathRow);
    fl->addRow("Имя ПК:", m_pcEdit);
    fl->addRow(m_showAllChk);
    fl->addRow(save);
    root->addWidget(box);
    root->addStretch();

    connect(browse, &QPushButton::clicked, [this]{
        QString p = QFileDialog::getExistingDirectory(this, "Выберите папку", m_pathEdit->text());
        if (!p.isEmpty()) m_pathEdit->setText(p);
    });
    connect(save, &QPushButton::clicked, this, &MainWindow::saveSettings);
    return w;
}

void MainWindow::saveSettings() {
    auto& st = SettingsManager::instance();
    st.setReportsPath(m_pathEdit->text());
    st.setPcName(m_pcEdit->text());
    st.setShowAllReports(m_showAllChk->isChecked());
    m_rm.setBasePath(st.reportsPath());
    
    // Обновляем видимость вкладки Админ
    updateAdminTabVisibility();
    
    refreshAllLists();
    QMessageBox::information(this, "OK", "Настройки сохранены.");
}

void MainWindow::updateAdminTabVisibility() {
    bool isAdmin = SettingsManager::instance().showAllReports();
    int adminIndex = m_tabs->indexOf(m_adminTabWidget);
    if (adminIndex != -1) {
        m_tabs->setTabVisible(adminIndex, isAdmin);
    }
}

// ---------- GLOBAL UPDATE ----------
void MainWindow::refreshAllLists() {
    auto& st = SettingsManager::instance();
    QString me = st.pcName();
    
    if (st.showAllReports()) {
        QList<Report> open;
        for (auto& x : m_rm.loadAll()) if (x.status != "resolved") open << x;
        m_userModel->setReports(open);
    } else {
        m_userModel->setReports(m_rm.loadByAuthor(me, false));
    }
    m_userTable->resizeColumnsToContents();

    QList<Report> openReports;
    for (auto& r : m_rm.loadAll()) if (r.status != "resolved") openReports << r;
    m_adminModel->setReports(openReports);
    m_adminTable->resizeColumnsToContents();

    m_archiveModel->setReports(m_rm.loadArchive());
    m_archiveTable->resizeColumnsToContents();
    
    updateTabsUnreadCount();
}

void MainWindow::autoRefresh() {
    QString currentUserId = m_currentUserId;
    QString currentAdminId = m_currentAdminId;

    refreshAllLists();

    if (!currentUserId.isEmpty()) {
        bool found = false;
        for (int i = 0; i < m_userModel->rowCount(QModelIndex()); ++i) {
            if (m_userModel->at(i).id == currentUserId) { found = true; break; }
        }
        if (found) onUserSelectionChanged();
    }

    if (!currentAdminId.isEmpty()) {
        bool found = false;
        for (int i = 0; i < m_adminModel->rowCount(QModelIndex()); ++i) {
            if (m_adminModel->at(i).id == currentAdminId) { found = true; break; }
        }
        if (found) onAdminSelectionChanged();
    }
}

void MainWindow::updateTabsUnreadCount() {
    int unreadUser = 0;
    int unreadAdmin = 0;
    
    for (const auto& r : m_rm.loadAll()) {
        int userUnread = 0;
        int adminUnread = 0;
        for (const auto& m : r.messages) {
            if (!m.isRead) {
                if (m.role == "admin") userUnread++;
                if (m.role == "user") adminUnread++;
            }
        }
        if (r.status != "resolved") {
            unreadUser += userUnread;
            unreadAdmin += adminUnread;
        }
    }

    QString userTabText = "Мой отчёт";
    if (unreadUser > 0) userTabText = QString("Мой отчёт (%1)").arg(unreadUser);
    
    QString adminTabText = "Админ";
    if (unreadAdmin > 0) adminTabText = QString("Админ (%1)").arg(unreadAdmin);

    m_tabs->setTabText(0, userTabText);
    m_tabs->setTabText(1, adminTabText);
}