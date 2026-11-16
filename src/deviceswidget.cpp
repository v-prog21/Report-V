#include "deviceswidget.h"

#include "devicedetaildialog.h"
#include "devicemodel.h"
#include "deviceutils.h"
#include "fgisclient.h"
#include "fgissearchdialog.h"
#include "settingsmanager.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

DevicesWidget::DevicesWidget(QWidget* parent)
    : QWidget(parent)
    , m_repo(SettingsManager::instance().devicesDbPath())
    , m_fgis(new FgisClient(this))
{
    setObjectName(QStringLiteral("page"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 12);
    root->setSpacing(10);

    root->addWidget(buildToolbar());

    m_model = new DeviceTableModel(this);
    m_proxy = new DeviceFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(42);

    QHeaderView* header = m_table->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(DeviceTableModel::ColNum, QHeaderView::Fixed);
    header->setSectionResizeMode(DeviceTableModel::ColName, QHeaderView::Stretch);
    header->setSectionResizeMode(DeviceTableModel::ColFactory, QHeaderView::Interactive);
    header->setSectionResizeMode(DeviceTableModel::ColVerification, QHeaderView::Interactive);
    header->setSectionResizeMode(DeviceTableModel::ColIndicator, QHeaderView::Fixed);
    m_table->setColumnWidth(DeviceTableModel::ColNum, 56);
    m_table->setColumnWidth(DeviceTableModel::ColFactory, 180);
    m_table->setColumnWidth(DeviceTableModel::ColVerification, 260);
    m_table->setColumnWidth(DeviceTableModel::ColIndicator, 142);

    m_table->setItemDelegateForColumn(DeviceTableModel::ColIndicator,
                                      new DeviceStatusDelegate(m_table));
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(DeviceTableModel::ColName, Qt::AscendingOrder);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(m_table);

    m_stack = new QStackedWidget;
    m_stack->addWidget(content);
    m_stack->addWidget(buildEmptyPage());
    root->addWidget(m_stack, 1);
    root->addWidget(buildBottomBar());

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(140);

    connect(m_search, &QLineEdit::textChanged, m_searchTimer, qOverload<>(&QTimer::start));
    connect(m_searchTimer, &QTimer::timeout, this, [this] {
        applySearch(m_search->text());
    });
    connect(m_statusFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        m_proxy->setStatusFilter(m_statusFilter->currentData().toInt());
        updateStats();
    });
    connect(m_refreshButton, &QPushButton::clicked, this, [this] { startImport(true); });
    connect(m_fgisSearchButton, &QPushButton::clicked, this, &DevicesWidget::openFgisSearch);
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
        openDetailsForCurrent();
    });
    connect(m_table, &QTableView::customContextMenuRequested,
            this, &DevicesWidget::showContextMenu);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &DevicesWidget::updateSelectionActions);
    connect(m_detailsButton, &QPushButton::clicked, this, &DevicesWidget::openDetailsForCurrent);
    connect(m_fgisButton, &QPushButton::clicked, this, &DevicesWidget::openFgisForCurrent);
    connect(m_fgis, &FgisClient::statusMessage, this, &DevicesWidget::statusMessage);

    reloadFromCache();
}

QWidget* DevicesWidget::buildToolbar()
{
    auto* toolbar = new QWidget;
    auto* row = new QHBoxLayout(toolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Приборы"));
    title->setObjectName(QStringLiteral("sectionTitle"));
    row->addWidget(title);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Поиск по названию, заводскому или рег. номеру…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(260);
    m_search->setMaximumWidth(460);
    row->addWidget(m_search, 1);

    m_statusFilter = new QComboBox;
    m_statusFilter->addItem(QStringLiteral("Все приборы"), -1);
    m_statusFilter->addItem(QStringLiteral("Просроченные"), int(DeviceUtils::Expired));
    m_statusFilter->addItem(QStringLiteral("Истекают (30 дней)"), int(DeviceUtils::Expiring));
    m_statusFilter->addItem(QStringLiteral("Действующие"), int(DeviceUtils::Valid));
    m_statusFilter->addItem(QStringLiteral("Без данных"), int(DeviceUtils::Unknown));
    m_statusFilter->setMinimumWidth(170);
    row->addWidget(m_statusFilter);

    row->addStretch(1);

    m_fgisSearchButton = new QPushButton(QStringLiteral("Поиск в ФГИС"));
    m_fgisSearchButton->setObjectName(QStringLiteral("btnSecondary"));
    m_fgisSearchButton->setToolTip(QStringLiteral(
        "Поиск поверок через API ФГИС «Аршин» с получением прямой ссылки на карточку"));
    row->addWidget(m_fgisSearchButton);

    m_refreshButton = new QPushButton(QStringLiteral("Обновить из MDB"));
    m_refreshButton->setToolTip(QStringLiteral("Прочитать файл MDB и обновить локальный кэш приборов"));
    row->addWidget(m_refreshButton);

    return toolbar;
}

QWidget* DevicesWidget::buildBottomBar()
{
    auto* bar = new QWidget;
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    m_statsLabel = new QLabel;
    m_statsLabel->setTextFormat(Qt::RichText);
    row->addWidget(m_statsLabel, 1);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setFixedWidth(150);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    row->addWidget(m_progress);

    m_syncLabel = new QLabel;
    m_syncLabel->setObjectName(QStringLiteral("muted"));
    row->addWidget(m_syncLabel);

    m_detailsButton = new QPushButton(QStringLiteral("Сведения"));
    m_detailsButton->setObjectName(QStringLiteral("btnSecondary"));
    m_detailsButton->setEnabled(false);
    row->addWidget(m_detailsButton);

    m_fgisButton = new QPushButton(QStringLiteral("ФГИС «Аршин»"));
    m_fgisButton->setObjectName(QStringLiteral("btnSecondary"));
    m_fgisButton->setEnabled(false);
    row->addWidget(m_fgisButton);

    return bar;
}

QWidget* DevicesWidget::buildEmptyPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignCenter);

    layout->addStretch(1);

    auto* icon = new QLabel;
    icon->setAlignment(Qt::AlignCenter);
    const QIcon deviceIcon(QStringLiteral(":/icons/devices.svg"));
    icon->setPixmap(deviceIcon.pixmap(64, 64));
    layout->addWidget(icon);

    auto* title = new QLabel(QStringLiteral("База приборов пуста"));
    title->setObjectName(QStringLiteral("emptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_emptyStatus = new QLabel;
    m_emptyStatus->setObjectName(QStringLiteral("emptyText"));
    m_emptyStatus->setAlignment(Qt::AlignCenter);
    m_emptyStatus->setWordWrap(true);
    m_emptyStatus->setMaximumWidth(520);
    layout->addWidget(m_emptyStatus, 0, Qt::AlignHCenter);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    auto* importButton = new QPushButton(QStringLiteral("Загрузить из MDB"));
    auto* searchButton = new QPushButton(QStringLiteral("Поиск в ФГИС (API)"));
    searchButton->setObjectName(QStringLiteral("btnSecondary"));
    auto* settingsButton = new QPushButton(QStringLiteral("Открыть настройки"));
    settingsButton->setObjectName(QStringLiteral("btnSecondary"));
    buttons->addWidget(importButton);
    buttons->addWidget(searchButton);
    buttons->addWidget(settingsButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    layout->addStretch(1);

    connect(importButton, &QPushButton::clicked, this, [this] { startImport(true); });
    connect(searchButton, &QPushButton::clicked, this, &DevicesWidget::openFgisSearch);
    connect(settingsButton, &QPushButton::clicked, this, &DevicesWidget::requestSettings);
    return page;
}

void DevicesWidget::applySearch(const QString& text)
{
    m_proxy->setSearchText(text);
    updateStats();
}

void DevicesWidget::reloadFromCache()
{
    const int sortColumn = m_proxy->sortColumn();
    const Qt::SortOrder sortOrder = m_proxy->sortOrder();

    QString currentId;
    if (const QModelIndex current = m_table->currentIndex(); current.isValid()) {
        const QModelIndex source = m_proxy->mapToSource(current);
        currentId = m_model->deviceAt(source.row()).id;
    }

    m_model->setDevices(m_repo.loadDevices());
    if (sortColumn >= 0)
        m_proxy->sort(sortColumn, sortOrder);

    if (!currentId.isEmpty()) {
        for (int row = 0; row < m_model->rowCount(); ++row) {
            if (m_model->deviceAt(row).id != currentId)
                continue;
            const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(row, 0));
            if (proxyIndex.isValid()) {
                m_table->setCurrentIndex(proxyIndex);
                m_table->selectRow(proxyIndex.row());
            }
            break;
        }
    }

    updateStats();
    updateEmptyState();
    updateSelectionActions();
}

void DevicesWidget::updateStats()
{
    if (!m_model || !m_proxy)
        return;

    int expired = 0;
    int expiring = 0;
    int valid = 0;
    int unknown = 0;

    for (int row = 0; row < m_model->rowCount(); ++row) {
        const Device d = m_model->deviceAt(row);
        switch (DeviceUtils::bucket(d.active, d.latestEnd)) {
        case DeviceUtils::Expired:  ++expired; break;
        case DeviceUtils::Expiring: ++expiring; break;
        case DeviceUtils::Valid:    ++valid; break;
        case DeviceUtils::Unknown:  ++unknown; break;
        }
    }

    const int total = m_model->rowCount();
    const int shown = m_proxy->rowCount();
    const QString shownPart = shown == total
                                  ? QStringLiteral("Всего: <b>%1</b>").arg(total)
                                  : QStringLiteral("Показано: <b>%1</b> из %2").arg(shown).arg(total);

    m_statsLabel->setText(QStringLiteral(
        "%1 &nbsp;·&nbsp; <span style='color:#047857;'>Действует: %2</span> &nbsp;·&nbsp; "
        "<span style='color:#b45309;'>Истекает: %3</span> &nbsp;·&nbsp; "
        "<span style='color:#b91c1c;'>Просрочено: %4</span> &nbsp;·&nbsp; "
        "<span style='color:#64748b;'>Без данных: %5</span>")
        .arg(shownPart).arg(valid).arg(expiring).arg(expired).arg(unknown));

    const QString lastSync = m_repo.lastSync();
    m_syncLabel->setText(lastSync.isEmpty()
                             ? QStringLiteral("Синхронизация не выполнялась")
                             : QStringLiteral("Обновлено: %1").arg(lastSync));
}

void DevicesWidget::updateEmptyState()
{
    const bool empty = m_model->rowCount() == 0;
    m_stack->setCurrentIndex(empty ? 1 : 0);

    const QString mdbPath = SettingsManager::instance().mdbPath();
    if (mdbPath.trimmed().isEmpty()) {
        m_emptyStatus->setText(QStringLiteral(
            "Укажите файл Microsoft Access (MDB) в настройках — приборы будут "
            "загружены в локальный кэш и доступны без повторного чтения MDB."));
    } else if (!QFileInfo::exists(mdbPath)) {
        m_emptyStatus->setText(QStringLiteral("Файл MDB не найден:\n%1").arg(mdbPath));
    } else if (empty) {
        m_emptyStatus->setText(QStringLiteral(
            "Нажмите «Загрузить из MDB», чтобы прочитать базу приборов. "
            "Данные сохранятся локально, и приложение будет работать быстрее."));
    }
}

void DevicesWidget::updateSelectionActions()
{
    const QModelIndex index = m_table->currentIndex();
    const bool hasSelection = index.isValid();
    m_detailsButton->setEnabled(hasSelection);

    bool hasDocument = false;
    if (hasSelection) {
        const QModelIndex source = m_proxy->mapToSource(index);
        const Device d = m_model->deviceAt(source.row());
        const QList<DeviceTest> tests = m_repo.testsForDevice(d.id);
        for (const DeviceTest& t : tests) {
            if (!t.numDoc.trimmed().isEmpty()) {
                hasDocument = true;
                break;
            }
        }
    }
    m_fgisButton->setEnabled(hasDocument);
}

void DevicesWidget::openDetailsForCurrent()
{
    const QModelIndex index = m_table->currentIndex();
    if (!index.isValid())
        return;

    const QModelIndex source = m_proxy->mapToSource(index);
    const Device d = m_model->deviceAt(source.row());
    if (d.id.isEmpty())
        return;

    const QList<DeviceTest> tests = m_repo.testsForDevice(d.id);
    DeviceDetailDialog dialog(d, tests, this);
    connect(&dialog, &DeviceDetailDialog::fgisRequested, m_fgis, &FgisClient::openDocument);
    dialog.exec();
}

void DevicesWidget::openFgisForCurrent()
{
    const QModelIndex index = m_table->currentIndex();
    if (!index.isValid())
        return;

    const QModelIndex source = m_proxy->mapToSource(index);
    const Device d = m_model->deviceAt(source.row());
    const QList<DeviceTest> tests = m_repo.testsForDevice(d.id);
    for (const DeviceTest& t : tests) {
        if (!t.numDoc.trimmed().isEmpty()) {
            m_fgis->openDocument(t.numDoc);
            return;
        }
    }
    emit statusMessage(QStringLiteral("У прибора нет номера документа поверки."));
}

void DevicesWidget::openFgisSearch()
{
    FgisSearchDialog dialog(this);
    const QModelIndex index = m_table->currentIndex();
    if (index.isValid()) {
        const QModelIndex source = m_proxy->mapToSource(index);
        const Device d = m_model->deviceAt(source.row());
        const QList<DeviceTest> tests = m_repo.testsForDevice(d.id);
        QString initial;
        for (const DeviceTest& t : tests) {
            if (!t.numDoc.trimmed().isEmpty()) {
                initial = t.numDoc;
                break;
            }
        }
        if (initial.isEmpty())
            initial = d.factoryNum.isEmpty() ? d.name : d.factoryNum;
        dialog.setInitialQuery(initial);
    } else if (m_search) {
        dialog.setInitialQuery(m_search->text());
    }
    dialog.exec();
}

void DevicesWidget::showContextMenu(const QPoint& position)
{
    const QModelIndex index = m_table->indexAt(position);
    if (!index.isValid())
        return;

    m_table->setCurrentIndex(index);
    m_table->selectRow(index.row());

    const QModelIndex source = m_proxy->mapToSource(index);
    const Device d = m_model->deviceAt(source.row());

    QMenu menu(this);
    QAction* detailsAction = menu.addAction(QStringLiteral("Дополнительные сведения"));
    QAction* fgisAction = menu.addAction(QStringLiteral("Открыть в ФГИС «Аршин»"));
    QAction* fgisSearchAction = menu.addAction(QStringLiteral("Поиск в ФГИС по API…"));
    bool hasDocument = false;
    QString firstDoc;
    const QList<DeviceTest> tests = m_repo.testsForDevice(d.id);
    for (const DeviceTest& t : tests) {
        if (!t.numDoc.trimmed().isEmpty()) {
            hasDocument = true;
            firstDoc = t.numDoc;
            break;
        }
    }
    fgisAction->setEnabled(hasDocument);
    menu.addSeparator();
    QAction* copyName = menu.addAction(QStringLiteral("Копировать название"));
    QAction* copyFactory = menu.addAction(QStringLiteral("Копировать заводской номер"));

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(position));
    if (chosen == detailsAction) {
        openDetailsForCurrent();
    } else if (chosen == fgisAction) {
        openFgisForCurrent();
    } else if (chosen == fgisSearchAction) {
        FgisSearchDialog dialog(this);
        dialog.setInitialQuery(!firstDoc.isEmpty() ? firstDoc
                                                   : (d.factoryNum.isEmpty() ? d.name : d.factoryNum));
        dialog.exec();
    } else if (chosen == copyName) {
        QApplication::clipboard()->setText(d.name);
    } else if (chosen == copyFactory) {
        QApplication::clipboard()->setText(d.factoryNum);
    }
}

void DevicesWidget::startImport(bool interactive)
{
    if (m_importing)
        return;

    const QString mdbPath = SettingsManager::instance().mdbPath().trimmed();
    if (mdbPath.isEmpty()) {
        if (interactive) {
            QMessageBox::information(
                this, QStringLiteral("База не выбрана"),
                QStringLiteral("Сначала укажите файл MDB в настройках приложения."));
            emit requestSettings();
        }
        return;
    }

    if (!QFileInfo::exists(mdbPath)) {
        QMessageBox::warning(
            this, QStringLiteral("Файл не найден"),
            QStringLiteral("Файл базы приборов не найден:\n%1").arg(mdbPath));
        return;
    }

    m_importInteractive = interactive;
    setImporting(true, QStringLiteral("Читаю данные из MDB…"));

    const QString dbPath = m_repo.dbPath();
    auto* watcher = new QFutureWatcher<DeviceImportResult>(this);
    m_importWatcher = watcher;

    connect(watcher, &QFutureWatcher<DeviceImportResult>::finished, this, [this, watcher] {
        const DeviceImportResult result = watcher->result();
        watcher->deleteLater();
        m_importWatcher = nullptr;
        finishImport(result);
    });

    watcher->setFuture(QtConcurrent::run([dbPath, mdbPath] {
        DeviceRepository repo(dbPath);
        return repo.importFromMdb(mdbPath);
    }));
}

void DevicesWidget::setImporting(bool importing, const QString& message)
{
    m_importing = importing;
    m_refreshButton->setEnabled(!importing);
    m_refreshButton->setText(importing ? QStringLiteral("Импорт…")
                                       : QStringLiteral("Обновить из MDB"));
    m_progress->setVisible(importing);
    if (!message.isEmpty()) {
        if (m_emptyStatus)
            m_emptyStatus->setText(message);
        emit statusMessage(message);
    }
}

void DevicesWidget::finishImport(const DeviceImportResult& result)
{
    setImporting(false);

    if (!result.ok) {
        emit statusMessage(QStringLiteral("Ошибка импорта базы приборов."));
        QMessageBox::warning(this, QStringLiteral("Не удалось обновить приборы"), result.error);
        updateEmptyState();
        return;
    }

    m_autoImportChecked = true;
    reloadFromCache();
    const QString message = QStringLiteral("Импортировано приборов: %1, сотрудников: %2.")
                                .arg(result.devices).arg(result.persons);
    emit statusMessage(message);
    m_syncLabel->setText(QStringLiteral("Обновлено: %1").arg(m_repo.lastSync()));
}

void DevicesWidget::onSettingsChanged()
{
    updateEmptyState();
    m_refreshButton->setToolTip(
        SettingsManager::instance().mdbPath().isEmpty()
            ? QStringLiteral("Укажите файл MDB в настройках")
            : QStringLiteral("Прочитать файл MDB и обновить локальный кэш приборов"));
    updateStats();
}

void DevicesWidget::optimizeDatabase()
{
    if (m_importing)
        return;

    if (m_repo.vacuum())
        emit statusMessage(QStringLiteral("Локальная база приборов оптимизирована."));
    else
        emit statusMessage(QStringLiteral("Не удалось оптимизировать базу приборов."));
}

int DevicesWidget::cachedDeviceCount() const
{
    return m_model ? m_model->rowCount() : 0;
}

QString DevicesWidget::lastSyncText() const
{
    return m_repo.lastSync();
}

void DevicesWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (m_autoImportChecked)
        return;
    m_autoImportChecked = true;

    if (m_model->rowCount() > 0)
        return;

    const QString mdbPath = SettingsManager::instance().mdbPath();
    if (!mdbPath.trimmed().isEmpty() && QFileInfo::exists(mdbPath))
        QTimer::singleShot(250, this, [this] { startImport(false); });
}
