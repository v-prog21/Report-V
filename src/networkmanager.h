#pragma once

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;
class QTimer;

struct PeerInfo {
    QString id;
    QString name;
    QString address;
    quint16 port = 0;
    qint64 lastSeen = 0;
    bool online = false;
};

/// Одноранговая сетевая служба Support IL.
///
/// Каждая копия приложения одновременно слушает TCP-порт (сервер для
/// сообщений и файлов) и рассылает UDP-присутствие в локальной сети.
/// Сообщения доставляются только участникам конкретного чата.
class NetworkManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkManager(QObject* parent = nullptr);
    ~NetworkManager() override;

    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    int listenPort() const { return m_listenPort; }
    QString selfId() const { return m_selfId; }
    QString selfName() const { return m_selfName; }

    /// Для автотестов: фиксированные идентификатор/имя/порты.
    void setIdentityOverride(const QString& id, const QString& name,
                             quint16 tcpPort = 0, quint16 discoveryPort = 0);

    QList<PeerInfo> peers() const;
    int onlineCount() const;

    /// Провайдер истории: возвращает чаты, где requester — участник.
    void setHistoryProvider(std::function<QJsonArray(const QString&)> provider);
    void setAttachmentResolver(std::function<QString(const QString&)> resolver);

    /// Адресная отправка сообщения/служебного пакета участникам чата.
    void sendMessage(const QString& messageId, const QJsonObject& payload,
                     const QSet<QString>& targets);
    void sendConversation(const QJsonObject& conversation, const QSet<QString>& targets);
    void sendArchive(const QString& id, const QSet<QString>& targets);
    void sendDelete(const QString& id, const QSet<QString>& targets);

    /// Обновляет имя текущего пользователя и немедленно рассылает presence.
    void updateSelfName(const QString& name);

    /// Запросить файл у конкретного участника сети.
    void downloadFromPeer(const PeerInfo& owner, const QString& filename,
                          qint64 expectedSize, const QString& destination);

    /// Служебный метод для автотестов: добавляет участника напрямую,
    /// не дожидаясь UDP-объявления.
    void injectPeerForTesting(const PeerInfo& peer);

    void refreshRoster();

signals:
    void rosterChanged();
    void statusMessage(const QString& message);
    void remoteMessage(const QJsonObject& payload);
    void historyReceived(const QJsonArray& items);
    void conversationArchived(const QString& id);
    void conversationDeleted(const QString& id);
    void downloadProgress(const QString& filename, qint64 received, qint64 total);
    void downloadFinished(const QString& filename, const QString& path,
                          bool ok, const QString& error);

private slots:
    void sendPresence();
    void sweepPeers();
    void retryPending();
    void onDatagram();
    void onNewConnection();

private:
    struct PendingMessage {
        QJsonObject payload;
        QSet<QString> targets;
    };

    bool bindDiscovery(quint16& usedPort);
    bool bindTcpServer(quint16 preferredPort, quint16& usedPort);

    void upsertPeer(const QHostAddress& address, const QJsonObject& presence);
    bool prunePeers();

    void deliverToPeer(const PeerInfo& peer, const QJsonObject& payload,
                       const QString& messageId, bool trackDelivery);
    void requestHistory(const PeerInfo& peer);

    void handleControl(QTcpSocket* socket, const QByteArray& line);
    void serveFileRequest(QTcpSocket* socket, const QJsonObject& request);

    static QByteArray jsonLine(const QJsonObject& object);
    static QString peerDisplay(const PeerInfo& peer);

    QString storageFilePath() const;
    void savePeers();

    QTcpServer* m_server = nullptr;
    QUdpSocket* m_discovery = nullptr;
    QTimer* m_presenceTimer = nullptr;
    QTimer* m_sweepTimer = nullptr;
    QTimer* m_retryTimer = nullptr;

    QString m_selfId;
    QString m_selfName;
    QString m_overrideId;
    QString m_overrideName;
    quint16 m_overrideTcpPort = 0;
    quint16 m_overrideDiscoveryPort = 0;
    quint16 m_listenPort = 0;
    quint16 m_discoveryPort = 0;
    bool m_running = false;
    bool m_multicast = false;

    QHash<QString, PeerInfo> m_peers;
    QHash<QString, PendingMessage> m_pending;
    QSet<QString> m_inFlight;
    QSet<QString> m_syncRequested;
    QHash<QString, QString> m_lastKnownNames;

    std::function<QJsonArray(const QString&)> m_historyProvider;
    std::function<QString(const QString&)> m_attachmentResolver;
};
