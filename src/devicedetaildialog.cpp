#include "devicedetaildialog.h"

#include "deviceutils.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString valueOrDash(const QString& value, const QString& dash = QStringLiteral("—"))
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? dash : trimmed;
}

QLabel* makeValueLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

DeviceDetailDialog::DeviceDetailDialog(const Device& device, const QList<DeviceTest>& tests,
                                       QWidget* parent)
    : QDialog(parent)
    , m_tests(tests)
{
    setWindowTitle(QStringLiteral("Дополнительные сведения о приборе"));
    resize(880, 680);
    setMinimumSize(720, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    const auto bucket = DeviceUtils::bucket(device.active, device.latestEnd);

    // --- Заголовок -------------------------------------------------------
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("ticketHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 14, 16, 14);
    headerLayout->setSpacing(12);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(3);
    auto* title = new QLabel(valueOrDash(device.name, QStringLiteral("Без названия")));
    title->setObjectName(QStringLiteral("ticketTitle"));
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QStringList subtitleParts;
    if (!device.factoryNum.trimmed().isEmpty())
        subtitleParts << QStringLiteral("Зав. № %1").arg(device.factoryNum);
    if (!device.regNum.trimmed().isEmpty())
        subtitleParts << QStringLiteral("Рег. № %1").arg(device.regNum);
    auto* subtitle = new QLabel(subtitleParts.isEmpty()
                                    ? QStringLiteral("Идентификатор: %1").arg(device.id)
                                    : subtitleParts.join(QStringLiteral("  ·  ")));
    subtitle->setObjectName(QStringLiteral("muted"));
    subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);

    titles->addWidget(title);
    titles->addWidget(subtitle);
    headerLayout->addLayout(titles, 1);

    auto* status = new QLabel(DeviceUtils::indicatorText(bucket));
    status->setObjectName(QStringLiteral("statusChip"));
    status->setStyleSheet(QStringLiteral(
        "QLabel#statusChip { background:%1; color:%2; border-radius:11px;"
        " padding:6px 14px; font-weight:700; }")
        .arg(DeviceUtils::indicatorBackground(bucket).name(),
             DeviceUtils::indicatorColor(bucket).name()));
    headerLayout->addWidget(status, 0, Qt::AlignTop);
    root->addWidget(header);

    // --- Основные сведения ----------------------------------------------
    auto* infoBox = new QGroupBox(QStringLiteral("Основная информация"));
    auto* grid = new QGridLayout(infoBox);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(9);
    grid->setContentsMargins(16, 22, 16, 14);

    int row = 0;
    auto addField = [&](const QString& caption, const QString& value, int column) {
        auto* captionLabel = new QLabel(caption);
        captionLabel->setObjectName(QStringLiteral("muted"));
        grid->addWidget(captionLabel, row, column * 2, Qt::AlignTop);
        grid->addWidget(makeValueLabel(value), row, column * 2 + 1);
    };

    addField(QStringLiteral("ID"), valueOrDash(device.id), 0);
    addField(QStringLiteral("Наличие поверки"),
             device.noTest == 1 ? QStringLiteral("С поверкой") : QStringLiteral("Без поверки"), 1);
    ++row;
    addField(QStringLiteral("Заводской номер"), valueOrDash(device.factoryNum), 0);
    addField(QStringLiteral("Регистрационный номер"), valueOrDash(device.regNum), 1);
    ++row;
    addField(QStringLiteral("Факторы"), DeviceUtils::decodeFactors(device.facIds), 0);
    addField(QStringLiteral("Поверок в базе"), QString::number(device.testCount), 1);
    ++row;
    addField(QStringLiteral("Погрешность"), valueOrDash(device.siErr), 0);
    addField(QStringLiteral("Последняя поверка"),
             device.latestEnd.trimmed().isEmpty()
                 ? QStringLiteral("Нет данных")
                 : DeviceUtils::formatDateRu(device.latestEnd), 1);
    ++row;
    addField(QStringLiteral("Условия эксплуатации"), valueOrDash(device.dopFld3), 0);
    addField(QStringLiteral("Статус"), DeviceUtils::indicatorTooltip(device.active, device.latestEnd), 1);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    root->addWidget(infoBox);

    // --- История поверок -------------------------------------------------
    auto* testsBox = new QGroupBox(QStringLiteral("История поверок"));
    auto* testsLayout = new QVBoxLayout(testsBox);
    testsLayout->setContentsMargins(12, 22, 12, 12);
    testsLayout->setSpacing(8);

    m_testsTable = new QTableWidget(tests.size(), 4);
    m_testsTable->setHorizontalHeaderLabels({
        QStringLiteral("Орган поверитель"),
        QStringLiteral("Номер документа"),
        QStringLiteral("Дата начала"),
        QStringLiteral("Дата окончания")
    });
    m_testsTable->verticalHeader()->setVisible(false);
    m_testsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_testsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_testsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_testsTable->setAlternatingRowColors(true);
    m_testsTable->setShowGrid(false);
    m_testsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_testsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_testsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_testsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_testsTable->verticalHeader()->setDefaultSectionSize(34);

    for (int i = 0; i < tests.size(); ++i) {
        const DeviceTest& t = tests.at(i);
        auto* org = new QTableWidgetItem(valueOrDash(t.orgReg));
        org->setToolTip(t.orgReg);
        auto* doc = new QTableWidgetItem(valueOrDash(t.numDoc));
        doc->setData(Qt::UserRole, t.numDoc);
        m_testsTable->setItem(i, 0, org);
        m_testsTable->setItem(i, 1, doc);
        m_testsTable->setItem(i, 2, new QTableWidgetItem(DeviceUtils::formatDateRu(t.beginDate)));
        m_testsTable->setItem(i, 3, new QTableWidgetItem(DeviceUtils::formatDateRu(t.endDate)));
    }
    testsLayout->addWidget(m_testsTable, 1);

    auto* linkHint = new QLabel(
        QStringLiteral("Двойной клик по строке открывает карточку поверки в ФГИС «Аршин»."));
    linkHint->setObjectName(QStringLiteral("muted"));
    testsLayout->addWidget(linkHint);
    root->addWidget(testsBox, 1);

    // --- Кнопки ----------------------------------------------------------
    auto* buttons = new QHBoxLayout;
    auto* copyButton = new QPushButton(QStringLiteral("Копировать ID"));
    copyButton->setObjectName(QStringLiteral("btnSecondary"));
    auto* fgisButton = new QPushButton(QStringLiteral("Открыть в ФГИС «Аршин»"));
    fgisButton->setObjectName(QStringLiteral("btnSecondary"));
    const bool hasDocument = std::any_of(
        tests.cbegin(), tests.cend(),
        [](const DeviceTest& test) { return !test.numDoc.trimmed().isEmpty(); });
    fgisButton->setEnabled(hasDocument);
    auto* closeButton = new QPushButton(QStringLiteral("Закрыть"));
    closeButton->setDefault(true);

    buttons->addWidget(copyButton);
    buttons->addWidget(fgisButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(copyButton, &QPushButton::clicked, this, [this, device] {
        QApplication::clipboard()->setText(device.id);
    });
    connect(fgisButton, &QPushButton::clicked, this, [this] {
        const QString doc = selectedNumDoc();
        if (!doc.isEmpty())
            emit fgisRequested(doc);
    });
    connect(m_testsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= m_tests.size())
            return;
        const QString doc = m_tests.at(row).numDoc.trimmed();
        if (!doc.isEmpty())
            emit fgisRequested(doc);
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    if (m_testsTable->rowCount() > 0)
        m_testsTable->selectRow(0);
}

QString DeviceDetailDialog::selectedNumDoc() const
{
    if (!m_testsTable)
        return {};

    int row = m_testsTable->currentRow();
    if (row < 0 && !m_tests.isEmpty())
        row = 0;
    if (row < 0 || row >= m_tests.size())
        return {};

    return m_tests.at(row).numDoc.trimmed();
}
