#include "reportmodel.h"

#include <QApplication>
#include <QColor>
#include <QDate>
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

void ReportModel::setOnlineIds(const QSet<QString>& ids)
{
    if (m_onlineIds == ids)
        return;
    m_onlineIds = ids;
    if (!m_reports.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_reports.size() - 1, ColCount - 1));
    }
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

QString ReportModel::previewText(const Report& report) const
{
    if (report.messages.isEmpty())
        return QStringLiteral("Нет сообщений");

    const Message& last = report.messages.constLast();
    QString body = last.text.simplified();
    if (body.isEmpty() && !last.attachments.isEmpty())
        body = QStringLiteral("📎 %1").arg(last.attachments.constFirst().visibleName());
    if (body.isEmpty())
        body = QStringLiteral("Вложение");

    if (report.kind == QLatin1String("group") && !last.senderId.isEmpty()
        && last.senderId != m_selfId) {
        const QString sender = last.senderName.trimmed().isEmpty()
                                   ? last.senderId.left(8)
                                   : last.senderName.trimmed();
        body = QStringLiteral("%1: %2").arg(sender, body);
    }
    return body;
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

    case LastMessageRole:
        return previewText(r);

    case KindRole:
        return direct ? QStringLiteral("direct") : QStringLiteral("group");

    case OnlineRole: {
        for (const Participant& p : r.participants) {
            if (p.id != m_selfId && m_onlineIds.contains(p.id))
                return true;
        }
        return false;
    }

    case ActivityTextRole: {
        const qint64 ts = r.lastActivityTs > 0 ? r.lastActivityTs : r.createdTs;
        if (ts <= 0)
            return QString();
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ts);
        const QDate today = QDate::currentDate();
        if (dt.date() == today)
            return dt.toString(QStringLiteral("HH:mm"));
        if (dt.date() == today.addDays(-1))
            return QStringLiteral("вчера");
        if (dt.date().year() == today.year())
            return dt.toString(QStringLiteral("dd.MM"));
        return dt.toString(QStringLiteral("dd.MM.yy"));
    }

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

ChatListDelegate::ChatListDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ChatListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    QColor background = QColor(QStringLiteral("#ffffff"));
    if (selected)
        background = QColor(QStringLiteral("#e7f3ff"));
    else if (hovered)
        background = QColor(QStringLiteral("#f5f8fc"));
    painter->fillRect(option.rect, background);

    painter->setPen(QColor(QStringLiteral("#f1f5f9")));
    painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

    const QRect r = option.rect.adjusted(14, 10, -14, -9);
    const bool group = index.data(ReportModel::KindRole).toString()
                       == QLatin1String("group");
    const bool online = index.data(ReportModel::OnlineRole).toBool();
    const int unread = index.data(ReportModel::UnreadRole).toInt();
    const QString title = index.data(Qt::DisplayRole).toString();
    const QString time = index.data(ReportModel::ActivityTextRole).toString();
    const QString preview = index.data(ReportModel::LastMessageRole).toString();

    QFont titleFont = option.font;
    titleFont.setWeight(unread > 0 ? QFont::Bold : QFont::DemiBold);
    if (titleFont.pointSizeF() > 0)
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.03);
    QFont smallFont = option.font;
    if (smallFont.pointSizeF() > 0)
        smallFont.setPointSizeF(smallFont.pointSizeF() * 0.9);

    // Индикатор: точка онлайн/офлайн для личных чатов, иконка группы.
    const QRect indicator(r.left(), r.top(), 18, 20);
    if (group) {
        painter->setFont(smallFont);
        painter->setPen(QColor(QStringLiteral("#64748b")));
        painter->drawText(indicator, Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("👥"));
    } else {
        painter->setPen(Qt::NoPen);
        painter->setBrush(online ? QColor(QStringLiteral("#31a24c"))
                                 : QColor(QStringLiteral("#9aa0a6")));
        painter->drawEllipse(QPointF(indicator.left() + 5, indicator.center().y()), 4, 4);
    }

    const int textLeft = r.left() + 24;

    // Время справа в первой строке.
    const QFontMetrics smallFm(smallFont);
    const int timeWidth = qMin(smallFm.horizontalAdvance(time), 64);
    const QRect timeRect(r.right() - timeWidth, r.top(), timeWidth, 20);
    painter->setFont(smallFont);
    painter->setPen(QColor(QStringLiteral("#94a3b8")));
    painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter, time);

    // Имя чата.
    const QFontMetrics titleFm(titleFont);
    const int titleWidth = qMax(0, timeRect.left() - textLeft - 8);
    painter->setFont(titleFont);
    painter->setPen(QColor(QStringLiteral("#0f172a")));
    painter->drawText(QRect(textLeft, r.top(), titleWidth, 20),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      titleFm.elidedText(title, Qt::ElideRight, titleWidth));

    // Бейдж непрочитанных справа во второй строке.
    int badgeWidth = 0;
    if (unread > 0) {
        QFont badgeFont = smallFont;
        badgeFont.setBold(true);
        const QFontMetrics badgeFm(badgeFont);
        const QString text = unread > 99 ? QStringLiteral("99+")
                                         : QString::number(unread);
        badgeWidth = qMax(20, badgeFm.horizontalAdvance(text) + 10);
        const int badgeHeight = 18;
        const QRect badge(r.right() - badgeWidth, r.bottom() - badgeHeight,
                          badgeWidth, badgeHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#2f80ed")));
        painter->drawRoundedRect(badge, 9, 9);
        painter->setFont(badgeFont);
        painter->setPen(Qt::white);
        painter->drawText(badge, Qt::AlignCenter, text);
    }

    // Последнее сообщение.
    const QFontMetrics previewFm(smallFont);
    const int previewWidth = qMax(0, r.right() - badgeWidth
                                         - (badgeWidth > 0 ? 8 : 0) - textLeft);
    const QRect previewRect(textLeft, r.bottom() - 20, previewWidth, 20);
    painter->setFont(smallFont);
    painter->setPen(unread > 0 ? QColor(QStringLiteral("#334155"))
                               : QColor(QStringLiteral("#64748b")));
    painter->drawText(previewRect, Qt::AlignLeft | Qt::AlignVCenter,
                      previewFm.elidedText(preview, Qt::ElideRight, previewWidth));

    painter->restore();
}

QSize ChatListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(300, 68);
}
