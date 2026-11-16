#include "networkmanager.h"

#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QPointer>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace {

constexpr int kDiscoveryIntervalMs = 3000;
constexpr int kSweepIntervalMs = 1000;
constexpr int kRetryIntervalMs = 3000;
constexpr qint64 kPeerTimeoutMs = 9000;
constexpr qint64 kPeerForgetMs = 7LL * 24 * 60 * 60 * 1000;
constexpr auto kMulticastGroup = "239.255.77.77";

QString sanitizeFileName(const QString& name)
{
    // Оставляем только последний компонент пути, причём учитываем оба
    // вида разделителей: имя приходит по сети и может быть враждебным.
    QString base = name;
    int slash = base.lastIndexOf(QLatin1Char('/'));
    const int backslash = base.lastIndexOf(QLatin1Char('\\'));
    if (backslash > slash)
        slash = backslash;
    if (slash >= 0)
        base = base.mid(slash + 1);
    base = base.trimmed();

    // Защита от выхода из каталога вложений: "." и ".." недопустимы.
    if (base == QLatin1String(".") || base == QLatin1String("..")) {
        base.clear();
        return base;
    }

    // Убираем символы, недопустимые в именах файлов Windows, и управляющие
    // символы. Имя в хранилище всегда должно быть безопасным.
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (QChar& ch : base) {
        if (ch.unicode() < 0x20 || invalid.contains(ch))
            ch = QLatin1Char('_');
    }

    // Windows не поддерживает имена, заканчивающиеся точкой или пробелом.
    while (base.endsWith(QLatin1Char('.')) || base.endsWith(QLatin1Char(' ')))
        base.chop(1);

    if (base.size() > 180)
        base = base.left(180);

    return base;
}

class FileSender : public QObject {
public:
    FileSender(QTcpSocket* socket, const QString& path)
        : QObject(socket)
        , m_socket(socket)
        , m_file(path)
    {
        connect(socket, &QTcpSocket::bytesWritten, this, [this] { pump(); });
        connect(socket, &QTcpSocket::disconnected, this, [this] { deleteLater(); });
    }

    bool start()
    {
        if (!m_file.open(QIODevice::ReadOnly))
            return false;
        pump();
        return true;
    }

private:
    void fail()
    {
        m_file.close();
        if (m_socket)
            m_socket->abort();
    }

    void pump()
    {
        if (m_pumping)
            return;
        if (!m_socket || m_socket->state() == QAbstractSocket::UnconnectedState)
            return;
        if (!m_file.isOpen())
            return;

        m_pumping = true;

        // Держим в буфере сокета не больше ~512 КБ, чтобы не блокировать
        // интерфейс и не съедать память на больших файлах.
        while (!m_file.atEnd() && m_socket->bytesToWrite() < 512 * 1024) {
            const QByteArray chunk = m_file.read(64 * 1024);
            if (chunk.isEmpty())
                break;

            const qint64 written = m_socket->write(chunk);
            if (written < 0) {
                // Сокет больше не принимает данные: передачу прекращаем,
                // иначе клиент будет вечно ждать недостающие байты.
                fail();
                m_pumping = false;
                return;
            }
            if (written < chunk.size()) {
                // Часть блока не поместилась в буфер — вернём указатель,
                // чтобы дослать эти байты при следующем bytesWritten.
                const qint64 back = chunk.size() - written;
                if (!m_file.seek(m_file.pos() - back)) {
                    fail();
                    m_pumping = false;
                    return;
                }
                break;
            }
        }
        m_socket->flush();

        const bool done = m_file.atEnd() && m_socket->bytesToWrite() == 0;
        m_pumping = false;

        if (done)
            m_socket->disconnectFromHost();
    }

    QTcpSocket* m_socket = nullptr;
    QFile m_file;
    bool m_pumping = false;
};

} // namespace

NetworkManager::NetworkManager(QObject* parent)
    : QObject(parent)
{
    m_selfId = SettingsManager::instance().networkId();
    m_selfName = SettingsManager::instance().pcName();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &NetworkManager::onNewConnection);

    m_discovery = new QUdpSocket(this);
    connect(m_discovery, &QUdpSocket::readyRead,
            this, &NetworkManager::onDatagram);

    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(kDiscoveryIntervalMs);
    connect(m_presenceTimer, &QTimer::timeout, this, &NetworkManager::sendPresence);

    m_sweepTimer = new QTimer(this);
    m_sweepTimer->setInterval(kSweepIntervalMs);
    connect(m_sweepTimer, &QTimer::timeout, this, &NetworkManager::sweepPeers);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(kRetryIntervalMs);
    connect(m_retryTimer, &QTimer::timeout, this, &NetworkManager::retryPending);
}

NetworkManager::~NetworkManager()
{
    stop();
}

void NetworkManager::setIdentityOverride(const QString& id, const QString& name,
                                         quint16 tcpPort, quint16 discoveryPort)
{
    m_overrideId = id;
    m_overrideName = name;
    m_overrideTcpPort = tcpPort;
    m_overrideDiscoveryPort = discoveryPort;
}

void NetworkManager::setHistoryProvider(
    std::function<QJsonArray(const QString&)> provider)
{
    m_historyProvider = std::move(provider);
}

void NetworkManager::setAttachmentResolver(std::function<QString(const QString&)> resolver)
{
    m_attachmentResolver = std::move(resolver);
}

bool NetworkManager::start()
{
    if (m_running)
        return true;

    if (!m_overrideId.isEmpty()) {
        m_selfId = m_overrideId;
        m_selfName = m_overrideName;
    } else {
        m_selfId = SettingsManager::instance().networkId();
        m_selfName = SettingsManager::instance().pcName();
    }

    quint16 usedPort = 0;
    const quint16 preferredPort = m_overrideTcpPort > 0
                                      ? m_overrideTcpPort
                                      : static_cast<quint16>(SettingsManager::instance().networkPort());
    if (!bindTcpServer(preferredPort, usedPort)) {
        emit statusMessage(QStringLiteral("Не удалось открыть сетевой порт."));
        return false;
    }
    m_listenPort = usedPort;

    // Если подписка не удастся, порт не должен остаться «старым»:
    // иначе рассылка presence уйдёт в никуда после перезапуска.
    m_discoveryPort = 0;
    if (!bindDiscovery(m_discoveryPort)) {
        emit statusMessage(
            QStringLiteral("Не удалось подписаться на сетевые объявления. "
                           "Поиск пользователей может не работать."));
    }

    // Восстанавливаем ранее замеченных участников (только в обычном режиме).
    if (m_overrideId.isEmpty()) {
        QFile input(storageFilePath());
        if (input.open(QIODevice::ReadOnly)) {
            const QJsonArray array = QJsonDocument::fromJson(input.readAll()).array();
            input.close();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            for (const QJsonValue& value : array) {
                const QJsonObject o = value.toObject();
                PeerInfo peer;
                peer.id = o.value(QStringLiteral("id")).toString();
                peer.name = o.value(QStringLiteral("name")).toString();
                peer.address = o.value(QStringLiteral("address")).toString();
                peer.port = static_cast<quint16>(o.value(QStringLiteral("port")).toInt());
                peer.lastSeen = o.value(QStringLiteral("lastSeen")).toVariant().toLongLong();
                peer.online = peer.lastSeen > 0 && (now - peer.lastSeen) <= kPeerTimeoutMs;
                if (!peer.id.isEmpty() && peer.id != m_selfId)
                    m_peers.insert(peer.id, peer);
            }
        }
    }

    m_running = true;
    m_presenceTimer->start();
    m_sweepTimer->start();
    m_retryTimer->start();
    sendPresence();
    emit rosterChanged();
    emit statusMessage(QStringLiteral("Сеть запущена на порту %1.").arg(m_listenPort));
    return true;
}

void NetworkManager::stop()
{
    if (!m_running)
        return;

    m_presenceTimer->stop();
    m_sweepTimer->stop();
    m_retryTimer->stop();

    if (m_server)
        m_server->close();
    if (m_discovery)
        m_discovery->close();

    for (QTcpSocket* socket : findChildren<QTcpSocket*>())
        socket->abort();

    m_pending.clear();
    m_inFlight.clear();
    m_syncRequested.clear();
    m_running = false;
}

bool NetworkManager::bindTcpServer(quint16 preferredPort, quint16& usedPort)
{
    for (quint16 offset = 0; offset < 20; ++offset) {
        const quint16 port = static_cast<quint16>(preferredPort + offset);
        if (m_server->listen(QHostAddress::AnyIPv4, port)) {
            usedPort = port;
            return true;
        }
    }
    return false;
}

bool NetworkManager::bindDiscovery(quint16& usedPort)
{
    const quint16 base = m_overrideDiscoveryPort > 0
                             ? m_overrideDiscoveryPort
                             : static_cast<quint16>(SettingsManager::instance().networkPort());
    if (m_discovery->bind(QHostAddress::AnyIPv4, base,
                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        usedPort = base;
    } else if (m_discovery->bind(QHostAddress::AnyIPv4, m_listenPort,
                                 QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        usedPort = m_listenPort;
    } else {
        return false;
    }

    // Групповой адрес позволяет видеть друг друга даже двум копиям
    // приложения на одном компьютере. Если multicast запрещён сетью,
    // остаётся широковещательная рассылка.
    m_multicast = m_discovery->joinMulticastGroup(
        QHostAddress(QString::fromLatin1(kMulticastGroup)));
    return true;
}

QList<PeerInfo> NetworkManager::peers() const
{
    return m_peers.values();
}

int NetworkManager::onlineCount() const
{
    int count = 0;
    for (const PeerInfo& peer : m_peers) {
        if (peer.online)
            ++count;
    }
    return count;
}

void NetworkManager::sendPresence()
{
    if (!m_discovery || m_discoveryPort == 0)
        return;

    QJsonObject presence;
    presence.insert(QStringLiteral("type"), QStringLiteral("presence"));
    presence.insert(QStringLiteral("id"), m_selfId);
    presence.insert(QStringLiteral("name"), m_selfName);
    presence.insert(QStringLiteral("port"), static_cast<int>(m_listenPort));
    presence.insert(QStringLiteral("version"), QStringLiteral("3.0"));
    presence.insert(QStringLiteral("timestamp"),
                    QDateTime::currentMSecsSinceEpoch());

    const QByteArray datagram = jsonLine(presence);

    if (m_multicast) {
        m_discovery->writeDatagram(datagram,
                                   QHostAddress(QString::fromLatin1(kMulticastGroup)),
                                   m_discoveryPort);
    }
    m_discovery->writeDatagram(datagram, QHostAddress::Broadcast, m_discoveryPort);

    // Явная рассылка по широковещательным адресам всех активных
    // интерфейсов: на компьютерах с несколькими сетевыми картами
    // QHostAddress::Broadcast не всегда выбирает нужный интерфейс.
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.broadcast().isNull())
                continue;
            m_discovery->writeDatagram(datagram, entry.broadcast(), m_discoveryPort);
        }
    }

    // Дополнительно — на localhost, чтобы экземпляры на одном ПК
    // гарантированно видели друг друга.
    m_discovery->writeDatagram(datagram, QHostAddress::LocalHost, m_discoveryPort);
}

void NetworkManager::updateSelfName(const QString& name)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty() || clean == m_selfName)
        return;
    m_selfName = clean;
    sendPresence();
    emit rosterChanged();
}

void NetworkManager::onDatagram()
{
    while (m_discovery->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(int(m_discovery->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        if (m_discovery->readDatagram(data.data(), data.size(), &sender, &senderPort) <= 0)
            continue;

        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject object = doc.object();
        if (object.value(QStringLiteral("type")).toString() != QLatin1String("presence"))
            continue;
        if (object.value(QStringLiteral("id")).toString() == m_selfId)
            continue;

        upsertPeer(sender, object);
    }
}

void NetworkManager::upsertPeer(const QHostAddress& address, const QJsonObject& presence)
{
    const QString id = presence.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty() || id == m_selfId)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto it = m_peers.find(id);
    const bool isNew = (it == m_peers.end());
    if (isNew)
        it = m_peers.insert(id, PeerInfo{id, QString(), QString(), 0, 0, false});

    PeerInfo& peer = it.value();
    const QString newName = presence.value(QStringLiteral("name")).toString().trimmed();
    const QString newAddress = address.toString();
    const quint16 newPort = static_cast<quint16>(presence.value(QStringLiteral("port")).toInt());
    const bool wasOnline = peer.online;

    const bool changed = isNew
                         || peer.name != newName
                         || peer.address != newAddress
                         || peer.port != newPort
                         || !wasOnline;

    peer.name = newName;
    peer.address = newAddress;
    peer.port = newPort;
    peer.lastSeen = now;
    peer.online = true;

    const bool pruned = prunePeers();

    if (changed || pruned) {
        emit rosterChanged();
        savePeers();
    }

    // Новому или вернувшемуся участнику запрашиваем историю.
    if ((isNew || !wasOnline) && peer.port > 0) {
        QTimer::singleShot(120, this, [this, id] {
            if (m_peers.contains(id)) {
                const PeerInfo current = m_peers.value(id);
                if (current.online)
                    requestHistory(current);
            }
        });
    }

    // Сообщения, адресованные этому участнику, отправляем сразу.
    if (peer.port > 0 && peer.online)
        QTimer::singleShot(0, this, [this] { retryPending(); });
}

bool NetworkManager::prunePeers()
{
    bool changed = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        PeerInfo& peer = it.value();
        const bool online = peer.lastSeen > 0 && (now - peer.lastSeen) <= kPeerTimeoutMs;
        if (online != peer.online) {
            peer.online = online;
            if (!online)
                m_syncRequested.remove(peer.id);
            changed = true;
        }
        if (peer.lastSeen > 0 && now - peer.lastSeen > kPeerForgetMs) {
            m_syncRequested.remove(peer.id);
            it = m_peers.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    return changed;
}

void NetworkManager::sweepPeers()
{
    if (prunePeers()) {
        emit rosterChanged();
        savePeers();
    }
}

void NetworkManager::refreshRoster()
{
    emit rosterChanged();
}

void NetworkManager::injectPeerForTesting(const PeerInfo& peer)
{
    if (peer.id.isEmpty() || peer.id == m_selfId)
        return;

    PeerInfo copy = peer;
    copy.online = true;
    copy.lastSeen = QDateTime::currentMSecsSinceEpoch();
    m_peers.insert(copy.id, copy);
    emit rosterChanged();

    if (copy.port > 0) {
        QTimer::singleShot(50, this, [this, id = copy.id] {
            if (m_peers.contains(id))
                requestHistory(m_peers.value(id));
        });
    }
}

void NetworkManager::savePeers()
{
    if (!m_overrideId.isEmpty())
        return;

    QJsonArray array;
    for (const PeerInfo& peer : m_peers) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), peer.id);
        o.insert(QStringLiteral("name"), peer.name);
        o.insert(QStringLiteral("address"), peer.address);
        o.insert(QStringLiteral("port"), int(peer.port));
        o.insert(QStringLiteral("lastSeen"), peer.lastSeen);
        array.append(o);
    }

    const QString path = storageFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile output(path);
    if (output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        output.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

QString NetworkManager::storageFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/SupportIL");
    return dir + QStringLiteral("/peers.json");
}

void NetworkManager::sendMessage(const QString& messageId, const QJsonObject& payload,
                                 const QSet<QString>& inputTargets)
{
    if (!m_running || messageId.isEmpty())
        return;

    QSet<QString> targets = inputTargets;
    targets.remove(m_selfId);
    targets.remove(QString());
    if (targets.isEmpty())
        return;

    PendingMessage pending;
    pending.payload = payload;
    pending.targets = targets;
    m_pending.insert(messageId, pending);

    for (const QString& target : targets) {
        const PeerInfo peer = m_peers.value(target);
        if (peer.online && peer.port > 0)
            deliverToPeer(peer, payload, messageId, true);
    }
}

void NetworkManager::sendConversation(const QJsonObject& conversation,
                                      const QSet<QString>& targets)
{
    if (conversation.isEmpty())
        return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("conversation"));
    payload.insert(QStringLiteral("conversation"), conversation);
    sendMessage(QStringLiteral("conv_") + conversation.value(QStringLiteral("id")).toString(),
                payload, targets);
}

void NetworkManager::sendArchive(const QString& id, const QSet<QString>& targets)
{
    if (id.isEmpty())
        return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("archive"));
    payload.insert(QStringLiteral("id"), id);
    sendMessage(QStringLiteral("archive_") + id, payload, targets);
}

void NetworkManager::sendDelete(const QString& id, const QSet<QString>& targets)
{
    if (id.isEmpty())
        return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("delete"));
    payload.insert(QStringLiteral("id"), id);
    sendMessage(QStringLiteral("delete_") + id, payload, targets);
}

void NetworkManager::retryPending()
{
    if (m_pending.isEmpty())
        return;

    // Копируем очередь перед обходом: deliverToPeer может синхронно
    // получить ответ/ошибку и изменить m_pending. Итерация по контейнеру,
    // который меняется изнутри, — неопределённое поведение.
    QList<QPair<QString, PendingMessage>> jobs;
    jobs.reserve(m_pending.size());
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        jobs.append(qMakePair(it.key(), it.value()));

    for (const auto& job : jobs) {
        if (!m_pending.contains(job.first))
            continue;

        for (const QString& target : job.second.targets) {
            // Предыдущая доставка могла завершить сообщение полностью.
            if (!m_pending.contains(job.first))
                break;

            const auto peerIt = m_peers.constFind(target);
            if (peerIt == m_peers.cend()) {
                // Участник неизвестен: ждём его появления в сети.
                continue;
            }
            if (peerIt->online && peerIt->port > 0)
                deliverToPeer(*peerIt, job.second.payload, job.first, true);
        }

        // Если все адресаты подтвердили получение, запись уже удалена.
        const auto pendingIt = m_pending.find(job.first);
        if (pendingIt != m_pending.end() && pendingIt->targets.isEmpty())
            m_pending.erase(pendingIt);
    }
}

void NetworkManager::deliverToPeer(const PeerInfo& peer, const QJsonObject& payload,
                                   const QString& messageId, bool trackDelivery)
{
    if (peer.address.isEmpty() || peer.port == 0)
        return;

    const QString flightKey = messageId + QLatin1Char('|') + peer.id;
    if (trackDelivery && !messageId.isEmpty() && m_inFlight.contains(flightKey))
        return;
    if (trackDelivery && !messageId.isEmpty())
        m_inFlight.insert(flightKey);

    auto* socket = new QTcpSocket(this);
    auto buffer = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);
    QPointer<QTcpSocket> guard(socket);

    // Единая точка завершения: ack, ошибка и тайм-аут не должны
    // обрабатывать один и тот же сокет повторно (в т.ч. deleteLater).
    const auto finish = [this, guard, buffer, flightKey, done](bool abortNow) {
        if (*done)
            return;
        *done = true;
        buffer->clear();
        m_inFlight.remove(flightKey);
        if (!guard)
            return;
        if (abortNow)
            guard->abort();
        else
            guard->disconnectFromHost();
        guard->deleteLater();
    };

    connect(socket, &QTcpSocket::connected, socket, [socket, payload] {
        if (socket->write(jsonLine(payload)) < 0)
            socket->abort();
        else
            socket->flush();
    });

    connect(socket, &QTcpSocket::readyRead, socket,
            [this, socket, buffer, messageId, trackDelivery, peerKey = peer.id,
             finish, done] {
        if (*done)
            return;

        buffer->append(socket->readAll());
        const int newline = buffer->indexOf('\n');
        if (newline < 0)
            return;

        const QByteArray line = buffer->left(newline);
        QJsonParseError error{};
        const QJsonDocument reply = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !reply.isObject())
            return;

        const QJsonObject object = reply.object();
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("ok")) {
            if (trackDelivery && !messageId.isEmpty()) {
                auto pendingIt = m_pending.find(messageId);
                if (pendingIt != m_pending.end()) {
                    pendingIt->targets.remove(peerKey);
                    if (pendingIt->targets.isEmpty())
                        m_pending.erase(pendingIt);
                }
            }
            finish(false);
        }
    });

    connect(socket, &QAbstractSocket::errorOccurred, socket,
            [finish] { finish(true); });

    QTimer::singleShot(8000, this, [finish] { finish(true); });

    socket->connectToHost(QHostAddress(peer.address), peer.port);
}

void NetworkManager::requestHistory(const PeerInfo& peer)
{
    if (m_syncRequested.contains(peer.id))
        return;
    m_syncRequested.insert(peer.id);
    const QString peerId = peer.id;

    auto* socket = new QTcpSocket(this);
    auto buffer = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);
    QPointer<QTcpSocket> guard(socket);

    const auto finish = [this, guard, buffer, peerId, done](bool abortNow) {
        if (*done)
            return;
        *done = true;
        buffer->clear();
        // Снимаем отметку при любом завершении, чтобы историю можно было
        // запросить повторно (например, после долгого отсутствия в сети).
        m_syncRequested.remove(peerId);
        if (!guard)
            return;
        if (abortNow)
            guard->abort();
        else
            guard->disconnectFromHost();
        guard->deleteLater();
    };

    connect(socket, &QTcpSocket::connected, socket, [this, socket, done] {
        if (*done)
            return;
        QJsonObject request;
        request.insert(QStringLiteral("type"), QStringLiteral("sync_req"));
        request.insert(QStringLiteral("requester"), m_selfId);
        if (socket->write(jsonLine(request)) < 0) {
            socket->abort();
            return;
        }
        socket->flush();
    });

    connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer, finish, done] {
        if (*done)
            return;

        buffer->append(socket->readAll());
        const int newline = buffer->indexOf('\n');
        if (newline < 0)
            return;

        const QByteArray line = buffer->left(newline);
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            finish(true);
            return;
        }

        const QJsonObject object = doc.object();
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("history"))
            emit historyReceived(object.value(QStringLiteral("items")).toArray());

        // Успешный ответ: снимаем m_syncRequested, чтобы следующая
        // синхронизация (например, после переподключения) сработала снова.
        finish(false);
    });

    connect(socket, &QAbstractSocket::errorOccurred, socket,
            [finish] { finish(true); });

    QTimer::singleShot(15000, this, [finish] { finish(true); });

    socket->connectToHost(QHostAddress(peer.address), peer.port);
}

void NetworkManager::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (!socket)
            continue;

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const int newline = buffer->indexOf('\n');
            if (newline < 0)
                return;

            const QByteArray line = buffer->left(newline);
            buffer->remove(0, newline + 1);
            handleControl(socket, line);
        });

        connect(socket, &QAbstractSocket::disconnected, socket, [socket] {
            socket->deleteLater();
        });
    }
}

void NetworkManager::handleControl(QTcpSocket* socket, const QByteArray& line)
{
    const auto sendAck = [socket](const QString& id = QString()) {
        QJsonObject ack;
        ack.insert(QStringLiteral("type"), QStringLiteral("ok"));
        ack.insert(QStringLiteral("id"), id);
        socket->write(jsonLine(ack));
        socket->flush();
        socket->disconnectFromHost();
    };

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        socket->disconnectFromHost();
        return;
    }

    const QJsonObject object = doc.object();
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("message")) {
        emit remoteMessage(object);
        sendAck(object.value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("id")).toString());
        return;
    }

    if (type == QLatin1String("conversation")) {
        emit remoteMessage(object);
        sendAck(object.value(QStringLiteral("conversation")).toObject()
                    .value(QStringLiteral("id")).toString());
        return;
    }

    if (type == QLatin1String("archive")) {
        emit conversationArchived(object.value(QStringLiteral("id")).toString());
        sendAck(object.value(QStringLiteral("id")).toString());
        return;
    }

    if (type == QLatin1String("delete")) {
        emit conversationDeleted(object.value(QStringLiteral("id")).toString());
        sendAck(object.value(QStringLiteral("id")).toString());
        return;
    }

    if (type == QLatin1String("sync_req")) {
        const QString requester = object.value(QStringLiteral("requester")).toString();
        QJsonArray items;
        if (m_historyProvider && !requester.isEmpty())
            items = m_historyProvider(requester);

        QJsonObject history;
        history.insert(QStringLiteral("type"), QStringLiteral("history"));
        history.insert(QStringLiteral("items"), items);
        socket->write(jsonLine(history));
        socket->flush();
        socket->disconnectFromHost();
        return;
    }

    if (type == QLatin1String("file_req")) {
        serveFileRequest(socket, object);
        return;
    }

    QJsonObject ack;
    ack.insert(QStringLiteral("type"), QStringLiteral("err"));
    ack.insert(QStringLiteral("message"), QStringLiteral("unknown request"));
    socket->write(jsonLine(ack));
    socket->flush();
    socket->disconnectFromHost();
}

void NetworkManager::serveFileRequest(QTcpSocket* socket, const QJsonObject& request)
{
    const QString filename = sanitizeFileName(request.value(QStringLiteral("name")).toString());
    const QString resolved = m_attachmentResolver ? m_attachmentResolver(filename) : QString();

    if (filename.isEmpty() || resolved.isEmpty() || !QFileInfo::exists(resolved)
        || !QFileInfo(resolved).isFile()) {
        QJsonObject err;
        err.insert(QStringLiteral("type"), QStringLiteral("file_err"));
        err.insert(QStringLiteral("name"), filename);
        err.insert(QStringLiteral("message"), QStringLiteral("Файл не найден у отправителя."));
        socket->write(jsonLine(err));
        socket->flush();
        socket->disconnectFromHost();
        return;
    }

    const QFileInfo info(resolved);
    QJsonObject header;
    header.insert(QStringLiteral("type"), QStringLiteral("file_ok"));
    header.insert(QStringLiteral("name"), filename);
    header.insert(QStringLiteral("size"), info.size());
    socket->write(jsonLine(header));
    socket->flush();

    auto* sender = new FileSender(socket, resolved);
    if (!sender->start()) {
        socket->disconnectFromHost();
        sender->deleteLater();
    }
}

void NetworkManager::downloadFromPeer(const PeerInfo& owner, const QString& filename,
                                      qint64 expectedSize, const QString& destination)
{
    const QString safeName = sanitizeFileName(filename);
    if (owner.address.isEmpty() || owner.port == 0 || safeName.isEmpty()) {
        emit downloadFinished(safeName, QString(), false,
                              QStringLiteral("Отправитель не доступен."));
        return;
    }

    struct State {
        QByteArray buffer;
        qint64 received = 0;
        qint64 total = 0;
        bool headerParsed = false;
        bool finished = false;
        QFile file;
    };

    auto state = std::make_shared<State>();
    state->total = expectedSize;
    state->file.setFileName(destination + QStringLiteral(".part"));

    auto* socket = new QTcpSocket(this);
    QPointer<QTcpSocket> guard(socket);

    const auto finish = [this, guard, state, safeName, destination](bool ok,
                                                                    const QString& error) {
        if (state->finished)
            return;
        state->finished = true;

        if (state->file.isOpen())
            state->file.close();

        QString resultPath;
        if (ok) {
            const QString tempPath = state->file.fileName();
            QFile::remove(destination);
            if (QFile::rename(tempPath, destination))
                resultPath = destination;
            else {
                emit downloadFinished(safeName, QString(), false,
                                      QStringLiteral("Не удалось сохранить файл."));
                if (guard) {
                    guard->abort();
                    guard->deleteLater();
                }
                return;
            }
        } else {
            if (!state->file.fileName().isEmpty())
                QFile::remove(state->file.fileName());
        }

        if (ok)
            emit downloadFinished(safeName, resultPath, true, QString());
        else
            emit downloadFinished(safeName, QString(), false,
                                  error.isEmpty() ? QStringLiteral("Загрузка прервана.")
                                                  : error);

        if (guard) {
            guard->abort();
            guard->deleteLater();
        }
    };

    connect(socket, &QTcpSocket::connected, socket, [socket, safeName] {
        QJsonObject request;
        request.insert(QStringLiteral("type"), QStringLiteral("file_req"));
        request.insert(QStringLiteral("name"), safeName);
        if (socket->write(jsonLine(request)) < 0)
            socket->abort();
        else
            socket->flush();
    });

    connect(socket, &QTcpSocket::readyRead, socket,
            [this, socket, state, finish] {
        if (state->finished)
            return;

        state->buffer.append(socket->readAll());

        if (!state->headerParsed) {
            const int newline = state->buffer.indexOf('\n');
            if (newline < 0)
                return;

            const QByteArray headerLine = state->buffer.left(newline);
            state->buffer.remove(0, newline + 1);

            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(headerLine, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                finish(false, QStringLiteral("Некорректный ответ сервера."));
                return;
            }

            const QJsonObject object = doc.object();
            if (object.value(QStringLiteral("type")).toString() != QLatin1String("file_ok")) {
                finish(false, object.value(QStringLiteral("message")).toString(
                                  QStringLiteral("Файл не найден у отправителя.")));
                return;
            }

            state->total = object.value(QStringLiteral("size")).toVariant().toLongLong();
            if (!state->file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                finish(false, QStringLiteral("Не удалось создать файл."));
                return;
            }
            state->headerParsed = true;

            // Пустой файл: заголовка достаточно.
            if (state->total == 0) {
                state->file.flush();
                state->file.close();
                finish(true, QString());
                return;
            }
        }

        if (!state->buffer.isEmpty()) {
            const qint64 written = state->file.write(state->buffer);
            if (written > 0) {
                state->received += written;
                emit downloadProgress(state->file.fileName(), state->received, state->total);
            }
            state->buffer.clear();
        }

        if (state->total > 0 && state->received >= state->total) {
            state->file.flush();
            state->file.close();
            finish(true, QString());
        }
    });

    connect(socket, &QAbstractSocket::errorOccurred, socket,
            [finish] { finish(false, QStringLiteral("Ошибка соединения при загрузке.")); });

    connect(socket, &QAbstractSocket::disconnected, socket, [state, finish] {
        if (!state->finished)
            finish(false, QStringLiteral("Соединение прервано."));
    });

    QTimer::singleShot(300000, this, [guard, state, finish] {
        if (guard && guard->state() != QAbstractSocket::UnconnectedState
            && !state->finished) {
            finish(false, QStringLiteral("Превышено время ожидания загрузки."));
        }
    });

    socket->connectToHost(QHostAddress(owner.address), owner.port);
}

QByteArray NetworkManager::jsonLine(const QJsonObject& object)
{
    QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    data.append('\n');
    return data;
}

QString NetworkManager::peerDisplay(const PeerInfo& peer)
{
    return peer.name.trimmed().isEmpty() ? peer.id.left(8) : peer.name;
}
