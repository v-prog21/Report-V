#include "reportmodel.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace {

QString shortId(const QString& id)
{
    return id.left(8);
}

} // namespace

ReportModel::ReportModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void ReportModel::setSelfId(const QString& selfId)
{
    m_selfId = selfId;
}

void ReportModel::setReports(const QList<Report>& reports)
{
    beginResetModel();
    m_reports = reports;
    endResetModel();
}

void ReportModel::clear()
{
    beginResetModel();
    m_reports.clear();
    endResetModel();
}

Report ReportModel::at(int row) const
{
    if (row < 0 || row >= m_reports.size())
        return {};
    return m_reports.at(row);
}

bool ReportModel::contains(const QString& reportId) const
{
    for (const Report& report : m_reports) {
        if (report.id == reportId)
            return true;
    }
    return false;
}

bool ReportModel::updateFromReport(const Report& report)
{
    for (int row = 0; row < m_reports.size(); ++row) {
        if (m_reports.at(row).id != report.id)
            continue;
        m_reports[row] = report;
        const QModelIndex left = index(row, 0);
        const QModelIndex right = index(row, ColCount - 1);
        emit dataChanged(left, right);
        return true;
    }
    return false;
}

int ReportModel::unreadCount(const Report& report) const
{
    return report.unreadIncomingCount(m_selfId);
}

QString ReportModel::activityText(const Report& report)
{
    const qint64 ts = report.lastActivityTs > 0 ? report.lastActivityTs : report.createdTs;
    if (ts <= 0)
        return report.created;
    return QDateTime::fromMSecsSinceEpoch(ts)
        .toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

int ReportModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_reports.size();
}

int ReportModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant ReportModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_reports.size())
        return {};

    const Report& r = m_reports.at(index.row());
    const bool direct = r.kind == QLatin1String("direct");
    const bool archived = r.status == QLatin1String("resolved");
    const QString title = r.displayTitle(m_selfId);
    const QString typeText = direct
                                 ? QStringLiteral("Личный")
                                 : QStringLiteral("Группа · %1").arg(r.participants.size());
    const QString statusText = archived ? QStringLiteral("Архив")
                                        : QStringLiteral("Активен");
    const int unread = unreadCount(r);

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColId:       return r.id;
        case ColTitle:    return title;
        case ColType:     return typeText;
        case ColMessages: return r.messages.size();
        case ColStatus:   return statusText;
        case ColUpdated:  return activityText(r);
        }
        break;

    case Qt::TextAlignmentRole:
        if (index.column() == ColMessages || index.column() == ColStatus
            || index.column() == ColUpdated) {
            return int(Qt::AlignCenter);
        }
        return int(Qt::AlignVCenter | Qt::AlignLeft);

    case Qt::ToolTipRole: {
        QStringList lines;
        lines << title;
        lines << QStringLiteral("Тип: %1").arg(typeText);
        const QString summary = r.participantSummary(m_selfId);
        if (!summary.isEmpty())
            lines << QStringLiteral("Участники: %1").arg(summary);
        lines << QStringLiteral("Сообщений: %1").arg(r.messages.size());
        if (unread > 0)
            lines << QStringLiteral("Новых: %1").arg(unread);
        lines << QStringLiteral("Обновлён: %1").arg(activityText(r));
        return lines.join(QLatin1Char('\n'));
    }

    case Qt::ForegroundRole:
        if (index.column() == ColStatus)
            return archived ? QColor(QStringLiteral("#475569"))
                            : QColor(QStringLiteral("#1d4ed8"));
        if (index.column() == ColMessages && unread > 0)
            return QColor(QStringLiteral("#1d4ed8"));
        break;

    case Qt::FontRole:
        if ((index.column() == ColTitle || index.column() == ColMessages) && unread > 0) {
            QFont font;
            font.setBold(true);
            return font;
        }
        break;

    case SortRole:
        switch (index.column()) {
        case ColId:       return r.id;
        case ColTitle:    return title.toLower();
        case ColType:     return typeText.toLower();
        case ColMessages: return r.messages.size();
        case ColStatus:   return r.status;
        case ColUpdated:  return r.lastActivityTs > 0 ? r.lastActivityTs : r.createdTs;
        }
        break;

    case FilterRole: {
        QStringList names;
        for (const Participant& p : r.participants)
            names << p.name;
        return QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(title, typeText, r.description, r.id, names.join(QLatin1Char(' ')),
                 statusText)
            .toLower();
    }

    case StatusRole:
        return r.status;

    case ReportIdRole:
        return r.id;

    case UnreadRole:
        return unread;

    default:
        break;
    }

    return {};
}

QVariant ReportModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColId:       return QStringLiteral("ID");
    case ColTitle:    return QStringLiteral("Чат");
    case ColType:     return QStringLiteral("Тип");
    case ColMessages: return QStringLiteral("Сообщений");
    case ColStatus:   return QStringLiteral("Статус");
    case ColUpdated:  return QStringLiteral("Обновлён");
    default:          return {};
    }
}

ReportStatusDelegate::ReportStatusDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ReportStatusDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();

    if (QStyle* style = opt.widget ? opt.widget->style() : QApplication::style())
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const bool archived = index.data(ReportModel::StatusRole).toString()
                          == QLatin1String("resolved");
    const QString text = archived ? QStringLiteral("Архив") : QStringLiteral("Активен");
    const QColor fg = archived ? QColor(QStringLiteral("#475569"))
                               : QColor(QStringLiteral("#1d4ed8"));
    const QColor bg = archived ? QColor(QStringLiteral("#e2e8f0"))
                               : QColor(QStringLiteral("#dbeafe"));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont font = opt.font;
    font.setPointSizeF(font.pointSizeF() * 0.92);
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);

    const QFontMetrics fm(font);
    const int w = qMin(opt.rect.width() - 14, fm.horizontalAdvance(text) + 28);
    const int h = 23;
    const QRect badge(opt.rect.center().x() - w / 2, opt.rect.center().y() - h / 2, w, h);

    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRoundedRect(badge, h / 2.0, h / 2.0);

    painter->setPen(fg);
    painter->drawText(badge, Qt::AlignCenter, text);
    painter->restore();
}

QSize ReportStatusDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setWidth(qMax(size.width(), 90));
    return size;
}
