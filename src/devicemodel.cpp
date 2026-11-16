#include "devicemodel.h"

#include "deviceutils.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

DeviceTableModel::DeviceTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void DeviceTableModel::setDevices(const QList<Device>& devices)
{
    beginResetModel();
    m_devices = devices;
    endResetModel();
}

Device DeviceTableModel::deviceAt(int row) const
{
    if (row < 0 || row >= m_devices.size())
        return {};
    return m_devices.at(row);
}

int DeviceTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

int DeviceTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant DeviceTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size())
        return {};

    const Device& d = m_devices.at(index.row());
    const auto bucket = DeviceUtils::bucket(d.active, d.latestEnd);
    const QString name = d.name.trimmed().isEmpty() ? QStringLiteral("Без названия") : d.name;
    const QString factory = d.factoryNum.trimmed().isEmpty() ? QStringLiteral("—") : d.factoryNum;

    QString verification;
    if (!d.latestBegin.isEmpty() && !d.latestEnd.isEmpty()) {
        verification = QStringLiteral("%1 — %2")
                           .arg(DeviceUtils::formatDateRu(d.latestBegin),
                                DeviceUtils::formatDateRu(d.latestEnd));
    } else if (!d.latestBegin.isEmpty()) {
        verification = DeviceUtils::formatDateRu(d.latestBegin) + QStringLiteral(" —");
    } else if (!d.latestEnd.isEmpty()) {
        verification = QStringLiteral("— ") + DeviceUtils::formatDateRu(d.latestEnd);
    } else {
        verification = QStringLiteral("Нет данных");
    }

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColNum:          return index.row() + 1;
        case ColName:         return name;
        case ColFactory:      return factory;
        case ColVerification: return verification;
        case ColIndicator:    return DeviceUtils::indicatorShort(bucket);
        }
        break;

    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case ColNum:
        case ColFactory:
        case ColVerification:
        case ColIndicator:
            return int(Qt::AlignCenter);
        default:
            return int(Qt::AlignVCenter | Qt::AlignLeft);
        }

    case Qt::ToolTipRole: {
        QStringList lines;
        lines << QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());
        if (!d.regNum.trimmed().isEmpty())
            lines << QStringLiteral("Рег. номер: %1").arg(d.regNum.toHtmlEscaped());
        if (!factory.isEmpty())
            lines << QStringLiteral("Заводской номер: %1").arg(factory.toHtmlEscaped());
        lines << QStringLiteral("Поверка: %1").arg(verification.toHtmlEscaped());
        lines << QStringLiteral("Статус: %1").arg(DeviceUtils::indicatorTooltip(d.active, d.latestEnd));
        lines << QStringLiteral("<i>Двойной клик — подробные сведения</i>");
        return lines.join(QStringLiteral("<br>"));
    }

    case Qt::ForegroundRole:
        if (index.column() == ColVerification) {
            if (bucket == DeviceUtils::Expired)
                return QColor(QStringLiteral("#b91c1c"));
            if (bucket == DeviceUtils::Expiring)
                return QColor(QStringLiteral("#b45309"));
        }
        if (index.column() == ColFactory && d.factoryNum.trimmed().isEmpty())
            return QColor(QStringLiteral("#94a3b8"));
        break;

    case SortRole:
        switch (index.column()) {
        case ColNum: {
            bool ok = false;
            const qlonglong numeric = d.id.toLongLong(&ok);
            return ok ? QVariant(numeric) : QVariant(d.id);
        }
        case ColName:
            return name.toLower();
        case ColFactory:
            return d.factoryNum.trimmed().toLower();
        case ColVerification:
            return d.latestEnd;
        case ColIndicator:
            return int(bucket);
        }
        break;

    case FilterRole:
        return QStringLiteral("%1 %2 %3 %4")
            .arg(name, d.factoryNum, d.regNum, d.id)
            .toLower();

    case BucketRole:
        return int(bucket);

    case StatusTextRole:
        return DeviceUtils::indicatorText(bucket);

    case DeviceIdRole:
        return d.id;

    default:
        break;
    }

    return {};
}

QVariant DeviceTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColNum:          return QStringLiteral("№");
    case ColName:         return QStringLiteral("Название");
    case ColFactory:      return QStringLiteral("Заводской номер");
    case ColVerification: return QStringLiteral("Действие поверки");
    case ColIndicator:    return QStringLiteral("Индикатор");
    default:              return {};
    }
}

// =====================================================================
//  Прокси-модель: поиск, фильтр по статусу, нумерация после сортировки
// =====================================================================
DeviceFilterProxyModel::DeviceFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortRole(DeviceTableModel::SortRole);
    setFilterRole(DeviceTableModel::FilterRole);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

void DeviceFilterProxyModel::setSearchText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (m_search == normalized)
        return;
    m_search = normalized;
    invalidateFilter();
}

void DeviceFilterProxyModel::setStatusFilter(int bucket)
{
    if (m_statusFilter == bucket)
        return;
    m_statusFilter = bucket;
    invalidateFilter();
}

QVariant DeviceFilterProxyModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::DisplayRole && index.column() == DeviceTableModel::ColNum)
        return index.row() + 1;
    return QSortFilterProxyModel::data(index, role);
}

bool DeviceFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid())
        return false;

    if (!m_search.isEmpty()) {
        const QString haystack = idx.data(DeviceTableModel::FilterRole).toString();
        if (!haystack.contains(m_search))
            return false;
    }

    if (m_statusFilter >= 0) {
        const int bucket = idx.data(DeviceTableModel::BucketRole).toInt();
        if (bucket != m_statusFilter)
            return false;
    }

    return true;
}

// =====================================================================
//  Отрисовка индикатора в виде цветного «бейджа»
// =====================================================================
DeviceStatusDelegate::DeviceStatusDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void DeviceStatusDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();

    if (QStyle* style = opt.widget ? opt.widget->style() : QApplication::style())
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const int bucket = index.data(DeviceTableModel::BucketRole).toInt();
    const QString text = index.data(DeviceTableModel::StatusTextRole).toString();
    const QColor fg = DeviceUtils::indicatorColor(static_cast<DeviceUtils::IndicatorBucket>(bucket));
    const QColor bg = DeviceUtils::indicatorBackground(static_cast<DeviceUtils::IndicatorBucket>(bucket));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont font = opt.font;
    font.setPointSizeF(font.pointSizeF() * 0.92);
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);

    const QFontMetrics fm(font);
    const int w = qMin(opt.rect.width() - 14, fm.horizontalAdvance(text) + 26);
    const int h = 24;
    const QRect badge(opt.rect.center().x() - w / 2, opt.rect.center().y() - h / 2, w, h);

    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRoundedRect(badge, h / 2.0, h / 2.0);

    painter->setPen(fg);
    painter->drawText(badge, Qt::AlignCenter, text);
    painter->restore();
}

QSize DeviceStatusDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setWidth(qMax(size.width(), 132));
    return size;
}
