#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QThread>

#include <cstdio>
#include <functional>

#include "networkmanager.h"
#include "reportmanager.h"

namespace {

int g_failures = 0;

void check(bool ok, const QString& what)
{
    if (ok) {
        std::printf("  ok   %s\n", qPrintable(what));
    } else {
        std::printf("  FAIL %s\n", qPrintable(what));
        ++g_failures;
    }
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(5);
    }
    return predicate();
}

QString tempRoot(const QString& name)
{
    const QString path = QDir::tempPath() + QStringLiteral("/supportil_selftest_")
                         + name + QStringLiteral("_")
                         + QString::number(QCoreApplication::applicationPid());
    QDir(path).removeRecursively();
    QDir().mkpath(path);
    return path;
}

QString writeTempFile(const QString& dir, const QString& name, const QByteArray& content)
{
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + name;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(content);
    file.close();
    return path;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

ReportManager* g_reportsA = nullptr;
ReportManager* g_reportsB = nullptr;
void wireNetwork(NetworkManager& network, ReportManager& reports)
{
    QObject::connect(&network, &NetworkManager::remoteMessage,
                     [&reports](const QJsonObject& payload) {
        bool isNew = false;
        reports.importNetworkMessage(payload, isNew);
    });
    QObject::connect(&network, &NetworkManager::historyReceived,
                     [&reports](const QJsonArray& items) {
        for (const QJsonValue& value : items) {
            bool isNew = false;
            reports.importConversation(value.toObject(), isNew);
        }
    });
    QObject::connect(&network, &NetworkManager::conversationArchived,
                     [&reports](const QString& id) { reports.archiveConversation(id); });
    QObject::connect(&network, &NetworkManager::conversationDeleted,
                     [&reports](const QString& id) { reports.deleteConversation(id); });

    network.setHistoryProvider([&reports](const QString& requester) {
        return reports.historyPayloadsFor(requester);
    });
    network.setAttachmentResolver([&reports](const QString& filename) {
        return reports.getAttachmentPath(filename);
    });
}

// ---------------------------------------------------------------------
//  1. Хранилище чатов: личные/групповые чаты, участники, дедупликация.
// ---------------------------------------------------------------------
void testStorage()
{
    std::printf("\n[storage]\n");
    const QString root = tempRoot(QStringLiteral("storage"));

    ReportManager reports(root);
    reports.setIdentity(QStringLiteral("id-a"), QStringLiteral("Алиса"));

    const QString direct = reports.ensureDirectChat(QStringLiteral("id-b"),
                                                    QStringLiteral("Борис"));
    check(!direct.isEmpty(), QStringLiteral("создан личный чат"));

    Report chat = reports.loadOne(direct);
    check(chat.kind == QLatin1String("direct"), QStringLiteral("тип чата — личный"));
    check(chat.participants.size() == 2, QStringLiteral("в личном чате два участника"));
    check(chat.displayTitle(QStringLiteral("id-a")) == QStringLiteral("Борис"),
          QStringLiteral("название личного чата — имя собеседника"));
    check(reports.directChatId(QStringLiteral("id-a"), QStringLiteral("id-b"))
              == reports.directChatId(QStringLiteral("id-b"), QStringLiteral("id-a")),
          QStringLiteral("ID личного чата детерминирован"));

    const QString sourceFile = writeTempFile(
        root + QStringLiteral("/incoming"), QStringLiteral("report.txt"),
        QByteArrayLiteral("file body"));
    check(!sourceFile.isEmpty(), QStringLiteral("подготовлен файл для отправки"));

    QJsonObject payload;
    Message message;
    const bool sent = reports.sendLocalMessage(direct, QStringLiteral("Привет"),
                                               {sourceFile}, payload, message);
    check(sent, QStringLiteral("сообщение сохранено локально"));
    check(message.attachments.size() == 1, QStringLiteral("вложение добавлено"));
    check(payload.value(QStringLiteral("message")).toObject()
              .value(QStringLiteral("text")).toString() == QStringLiteral("Привет"),
          QStringLiteral("payload содержит текст сообщения"));

    if (!message.attachments.isEmpty()) {
        const Attachment& a = message.attachments.first();
        check(QFileInfo::exists(reports.getAttachmentPath(a.filename)),
              QStringLiteral("файл скопирован в папку вложений"));
        check(a.visibleName() == QStringLiteral("report.txt"),
              QStringLiteral("исходное имя файла сохраняется"));
        check(a.ownerId == QStringLiteral("id-a"),
              QStringLiteral("у вложения указан владелец"));
    }

    chat = reports.loadOne(direct);
    check(chat.unreadIncomingCount(QStringLiteral("id-b")) == 0,
          QStringLiteral("своё сообщение не считается непрочитанным"));

    // Входящее сообщение от собеседника.
    QJsonObject conversation = reports.exportConversation(direct);
    QJsonObject incoming;
    incoming.insert(QStringLiteral("id"), QStringLiteral("msg-from-b"));
    incoming.insert(QStringLiteral("senderId"), QStringLiteral("id-b"));
    incoming.insert(QStringLiteral("senderName"), QStringLiteral("Борис"));
    incoming.insert(QStringLiteral("text"), QStringLiteral("Ответ"));
    incoming.insert(QStringLiteral("timestamp"),
                    QDateTime::currentMSecsSinceEpoch());
    incoming.insert(QStringLiteral("isRead"), false);
    QJsonArray messages = conversation.value(QStringLiteral("messages")).toArray();
    messages.append(incoming);
    conversation.insert(QStringLiteral("messages"), messages);

    QJsonObject incomingPayload;
    incomingPayload.insert(QStringLiteral("type"), QStringLiteral("message"));
    incomingPayload.insert(QStringLiteral("conversation"), conversation);
    incomingPayload.insert(QStringLiteral("message"), incoming);

    bool isNew = false;
    check(reports.importNetworkMessage(incomingPayload, isNew),
          QStringLiteral("входящее сообщение импортировано"));
    check(isNew, QStringLiteral("новое сообщение помечено как новое"));
    check(reports.loadOne(direct).unreadIncomingCount(QStringLiteral("id-a")) == 1,
          QStringLiteral("непрочитанное входящее учтено"));

    isNew = true;
    reports.importNetworkMessage(incomingPayload, isNew);
    check(!isNew, QStringLiteral("повторная доставка не создаёт дубликат"));
    check(reports.loadOne(direct).messages.size() == 2,
          QStringLiteral("количество сообщений не изменилось"));

    reports.markConversationRead(direct);
    check(reports.loadOne(direct).unreadIncomingCount(QStringLiteral("id-a")) == 0,
          QStringLiteral("чат помечен прочитанным"));

    // Имя участника обновляется во всех чатах и сообщениях.
    reports.updateParticipantName(QStringLiteral("id-b"), QStringLiteral("Борис П."));
    chat = reports.loadOne(direct);
    check(chat.displayTitle(QStringLiteral("id-a")) == QStringLiteral("Борис П."),
          QStringLiteral("новое имя видно в заголовке чата"));
    bool renamedMessage = false;
    for (const Message& m : chat.messages) {
        if (m.senderId == QStringLiteral("id-b")
            && m.senderName == QStringLiteral("Борис П.")) {
            renamedMessage = true;
        }
    }
    check(renamedMessage, QStringLiteral("новое имя видно в сообщениях"));

    const QString group = reports.createGroupChat(
        QStringLiteral("Техподдержка"),
        {{QStringLiteral("id-b"), QStringLiteral("Борис")},
         {QStringLiteral("id-c"), QStringLiteral("Вера")}});
    check(!group.isEmpty(), QStringLiteral("создан групповой чат"));
    const QSet<QString> participants = reports.participantIds(group);
    check(participants.size() == 3 && participants.contains(QStringLiteral("id-a"))
              && participants.contains(QStringLiteral("id-b"))
              && participants.contains(QStringLiteral("id-c")),
          QStringLiteral("состав группы корректен"));
    check(reports.loadOne(group).displayTitle(QStringLiteral("id-a"))
              == QStringLiteral("Техподдержка"),
          QStringLiteral("название группы сохраняется"));

    // Добавление участников в уже существующую группу.
    QJsonObject grownConversation;
    check(reports.addParticipants(
              group,
              {{QStringLiteral("id-d"), QStringLiteral("Глеб")}},
              grownConversation),
          QStringLiteral("участник добавлен в группу"));
    const QSet<QString> grown = reports.participantIds(group);
    check(grown.size() == 4 && grown.contains(QStringLiteral("id-d")),
          QStringLiteral("новый участник появился в составе группы"));
    check(grownConversation.value(QStringLiteral("participants")).toArray().size() == 4,
          QStringLiteral("обновлённый состав уходит для рассылки"));

    QJsonObject duplicateConversation;
    check(reports.addParticipants(
              group,
              {{QStringLiteral("id-d"), QStringLiteral("Глеб")}},
              duplicateConversation),
          QStringLiteral("повторное добавление не ломает группу"));
    check(reports.participantIds(group).size() == 4,
          QStringLiteral("повторное добавление не создаёт дубликатов"));

    // Сообщение от участника со старой копией состава не должно
    // выкидывать недавно добавленных пользователей.
    QJsonObject stale = reports.exportConversation(group);
    QJsonArray staleParticipants;
    for (const QJsonValue& value : stale.value(QStringLiteral("participants")).toArray()) {
        const QJsonObject p = value.toObject();
        const QString pid = p.value(QStringLiteral("id")).toString();
        if (pid != QStringLiteral("id-d"))
            staleParticipants.append(p);
    }
    stale.insert(QStringLiteral("participants"), staleParticipants);
    bool staleNew = false;
    check(reports.importConversation(stale, staleNew),
          QStringLiteral("устаревший состав группы принят"));
    check(reports.participantIds(group).size() == 4,
          QStringLiteral("устаревший состав не теряет новых участников"));
    check(reports.loadOne(group).displayTitle(QStringLiteral("id-a"))
              == QStringLiteral("Техподдержка"),
          QStringLiteral("название группы не меняется"));

    const QJsonArray historyB = reports.historyPayloadsFor(QStringLiteral("id-b"));
    check(historyB.size() == 2, QStringLiteral("история отдаётся участнику"));
    const QJsonArray historyD = reports.historyPayloadsFor(QStringLiteral("id-d"));
    check(historyD.size() == 1, QStringLiteral("новый участник получает историю группы"));
    check(reports.historyPayloadsFor(QStringLiteral("id-x")).isEmpty(),
          QStringLiteral("посторонний не получает историю"));

    check(reports.archiveConversation(group), QStringLiteral("чат архивируется"));
    check(reports.loadOne(group).status == QLatin1String("resolved"),
          QStringLiteral("архивный чат имеет статус resolved"));
    check(reports.deleteConversation(group), QStringLiteral("чат удаляется"));
    check(reports.loadOne(group).id.isEmpty(), QStringLiteral("удалённый чат не загружается"));

    QDir(root).removeRecursively();
}

// ---------------------------------------------------------------------
//  2. Сеть: доставка сообщений, история, передача файла, дедупликация.
// ---------------------------------------------------------------------
void testNetwork()
{
    std::printf("\n[network]\n");

    const QString root = tempRoot(QStringLiteral("network"));
    const QString rootA = root + QStringLiteral("/a");
    const QString rootB = root + QStringLiteral("/b");
    QDir().mkpath(rootA);
    QDir().mkpath(rootB);

    const quint16 tcpA = 42610;
    const quint16 tcpB = 42611;
    const quint16 udpA = 42710;
    const quint16 udpB = 42711;

    {
        ReportManager reportsA(rootA);
        ReportManager reportsB(rootB);
        reportsA.setIdentity(QStringLiteral("id-a"), QStringLiteral("Алиса"));
        reportsB.setIdentity(QStringLiteral("id-b"), QStringLiteral("Борис"));
        g_reportsA = &reportsA;

        // Чаты создаются до обмена presence — получатель заберёт их
        // через синхронизацию истории.
        const QString direct = reportsA.ensureDirectChat(
            QStringLiteral("id-b"), QStringLiteral("Борис"));
        const QString group = reportsA.createGroupChat(
            QStringLiteral("Общая группа"),
            {{QStringLiteral("id-b"), QStringLiteral("Борис")},
             {QStringLiteral("id-c"), QStringLiteral("Вера")}});
        check(!direct.isEmpty() && !group.isEmpty(),
              QStringLiteral("чаты подготовлены на стороне А"));

        NetworkManager netA;
        NetworkManager netB;
        netA.setIdentityOverride(QStringLiteral("id-a"), QStringLiteral("Алиса"),
                                 tcpA, udpA);
        netB.setIdentityOverride(QStringLiteral("id-b"), QStringLiteral("Борис"),
                                 tcpB, udpB);
        wireNetwork(netA, reportsA);
        wireNetwork(netB, reportsB);

        check(netA.start(), QStringLiteral("сетевой сервис А запущен"));
        check(netB.start(), QStringLiteral("сетевой сервис Б запущен"));
        check(netA.listenPort() == tcpA && netB.listenPort() == tcpB,
              QStringLiteral("TCP-порты заняты"));

        PeerInfo peerA;
        peerA.id = QStringLiteral("id-a");
        peerA.name = QStringLiteral("Алиса");
        peerA.address = QStringLiteral("127.0.0.1");
        peerA.port = tcpA;
        peerA.online = true;

        PeerInfo peerB;
        peerB.id = QStringLiteral("id-b");
        peerB.name = QStringLiteral("Борис");
        peerB.address = QStringLiteral("127.0.0.1");
        peerB.port = tcpB;
        peerB.online = true;

        netA.injectPeerForTesting(peerB);
        netB.injectPeerForTesting(peerA);

        // История: Б должен получить личный и групповой чаты.
        const bool historyOk = waitFor([&] {
            return !reportsB.loadOne(direct).id.isEmpty()
                   && !reportsB.loadOne(group).id.isEmpty();
        });
        check(historyOk, QStringLiteral("история чатов синхронизирована по сети"));
        check(reportsB.loadOne(direct).displayTitle(QStringLiteral("id-b"))
                  == QStringLiteral("Алиса"),
              QStringLiteral("имя пользователя А видно на компьютере Б"));

        // Сообщение А -> Б.
        QJsonObject payload;
        Message message;
        check(reportsA.sendLocalMessage(direct, QStringLiteral("Привет, Борис"),
                                        {}, payload, message),
              QStringLiteral("сообщение А сохранено"));
        netA.sendMessage(message.id, payload, {QStringLiteral("id-b")});

        const bool delivered = waitFor([&] {
            for (const Message& m : reportsB.loadOne(direct).messages) {
                if (m.text == QStringLiteral("Привет, Борис"))
                    return true;
            }
            return false;
        });
        check(delivered, QStringLiteral("сообщение доставлено Б"));
        check(reportsB.loadOne(direct).unreadIncomingCount(QStringLiteral("id-b")) == 1,
              QStringLiteral("у Б появилось непрочитанное сообщение"));

        // Ответ Б -> А.
        QJsonObject replyPayload;
        Message reply;
        check(reportsB.sendLocalMessage(direct, QStringLiteral("Привет, Алиса"),
                                        {}, replyPayload, reply),
              QStringLiteral("ответ Б сохранён"));
        netB.sendMessage(reply.id, replyPayload, {QStringLiteral("id-a")});

        const bool replyDelivered = waitFor([&] {
            for (const Message& m : reportsA.loadOne(direct).messages) {
                if (m.text == QStringLiteral("Привет, Алиса"))
                    return true;
            }
            return false;
        });
        check(replyDelivered, QStringLiteral("ответ доставлен А"));

        // Повторная доставка того же сообщения не создаёт дубликат.
        const int before = reportsB.loadOne(direct).messages.size();
        netA.sendMessage(message.id, payload, {QStringLiteral("id-b")});
        waitFor([&] { return false; }, 700);
        check(reportsB.loadOne(direct).messages.size() == before,
              QStringLiteral("повторная доставка не дублирует сообщение"));

        // Передача файла по запросу.
        const QString filePath = writeTempFile(
            root + QStringLiteral("/outgoing"), QStringLiteral("данные.txt"),
            QByteArrayLiteral("network file content"));
        QJsonObject filePayload;
        Message fileMessage;
        check(reportsA.sendLocalMessage(direct, QStringLiteral("Файл"),
                                        {filePath}, filePayload, fileMessage),
              QStringLiteral("сообщение с файлом сохранено"));

        netA.sendMessage(fileMessage.id, filePayload, {QStringLiteral("id-b")});

        const bool fileMeta = waitFor([&] {
            const Report chat = reportsB.loadOne(direct);
            for (const Message& m : chat.messages) {
                if (!m.attachments.isEmpty())
                    return true;
            }
            return false;
        });
        check(fileMeta, QStringLiteral("Б получил описание вложения"));

        Attachment attachment;
        const Report chatB = reportsB.loadOne(direct);
        for (const Message& m : chatB.messages) {
            if (!m.attachments.isEmpty()) {
                attachment = m.attachments.first();
                break;
            }
        }
        check(!attachment.filename.isEmpty(), QStringLiteral("имя файла передано"));
        check(attachment.visibleName() == QStringLiteral("данные.txt"),
              QStringLiteral("исходное имя файла доставлено собеседнику"));
        check(!QFileInfo::exists(reportsB.getAttachmentPath(attachment.filename)),
              QStringLiteral("файл не скачивается автоматически"));

        bool downloadDone = false;
        bool downloadOk = false;
        QString downloadedName;
        QObject::connect(&netB, &NetworkManager::downloadFinished,
                         [&](const QString& name, const QString&, bool ok, const QString&) {
            downloadedName = name;
            downloadDone = true;
            downloadOk = ok;
        });

        const QString destination = reportsB.getAttachmentPath(attachment.filename);
        netB.downloadFromPeer(peerA, attachment.filename, attachment.size, destination);
        check(waitFor([&] { return downloadDone; }), QStringLiteral("загрузка файла завершена"));
        check(downloadOk, QStringLiteral("файл скачан без ошибок"));
        check(downloadedName == attachment.filename,
              QStringLiteral("имя скачанного файла совпадает"));
        check(readFile(destination) == QByteArrayLiteral("network file content"),
              QStringLiteral("содержимое файла совпадает"));
        reportsB.markAttachmentDownloaded(attachment.filename);
        check(reportsB.attachmentMeta(direct, attachment.filename).downloaded,
              QStringLiteral("вложение помечено скачанным"));

        netB.stop();
        netA.stop();
    }

    g_reportsA = nullptr;
    QDir(root).removeRecursively();
}

// ---------------------------------------------------------------------
//  3. Обнаружение в сети: presence, имена, обновление имени.
// ---------------------------------------------------------------------
void testDiscovery()
{
    std::printf("\n[discovery]\n");

    NetworkManager netA;
    NetworkManager netB;
    netA.setIdentityOverride(QStringLiteral("id-a3"), QStringLiteral("Алиса"),
                             42630, 42730);
    netB.setIdentityOverride(QStringLiteral("id-b3"), QStringLiteral("Борис"),
                             42631, 42730);

    check(netA.start(), QStringLiteral("сервис А запущен"));
    check(netB.start(), QStringLiteral("сервис Б запущен"));

    const bool found = waitFor([&] {
        bool a = false;
        bool b = false;
        for (const PeerInfo& peer : netA.peers()) {
            if (peer.id == QStringLiteral("id-b3") && peer.name == QStringLiteral("Борис"))
                a = true;
        }
        for (const PeerInfo& peer : netB.peers()) {
            if (peer.id == QStringLiteral("id-a3") && peer.name == QStringLiteral("Алиса"))
                b = true;
        }
        return a && b;
    }, 12000);
    check(found, QStringLiteral("оба пользователя нашли друг друга в сети"));

    netB.updateSelfName(QStringLiteral("Борис Новый"));
    const bool renamed = waitFor([&] {
        for (const PeerInfo& peer : netA.peers()) {
            if (peer.id == QStringLiteral("id-b3")
                && peer.name == QStringLiteral("Борис Новый")) {
                return true;
            }
        }
        return false;
    }, 12000);
    check(renamed, QStringLiteral("новое имя пользователя видно другому компьютеру"));

    netB.stop();
    netA.stop();
}

// ---------------------------------------------------------------------
//  4. Офлайн-очередь: сообщение ждёт появления получателя.
// ---------------------------------------------------------------------
void testOfflineQueue()
{
    std::printf("\n[offline queue]\n");
    const QString root = tempRoot(QStringLiteral("offline"));
    const QString rootA = root + QStringLiteral("/a");
    const QString rootB = root + QStringLiteral("/b");
    QDir().mkpath(rootA);
    QDir().mkpath(rootB);

    const quint16 tcpA = 42620;
    const quint16 tcpB = 42621;

    {
        ReportManager reportsA(rootA);
        ReportManager reportsB(rootB);
        reportsA.setIdentity(QStringLiteral("id-a2"), QStringLiteral("Алиса"));
        reportsB.setIdentity(QStringLiteral("id-b2"), QStringLiteral("Борис"));

        const QString direct = reportsA.ensureDirectChat(
            QStringLiteral("id-b2"), QStringLiteral("Борис"));

        NetworkManager netA;
        NetworkManager netB;
        netA.setIdentityOverride(QStringLiteral("id-a2"), QStringLiteral("Алиса"),
                                 tcpA, 42720);
        netB.setIdentityOverride(QStringLiteral("id-b2"), QStringLiteral("Борис"),
                                 tcpB, 42721);
        wireNetwork(netA, reportsA);
        wireNetwork(netB, reportsB);
        check(netA.start() && netB.start(),
              QStringLiteral("оба сервиса запущены"));

        // Б ещё не известен А: сообщение должно встать в очередь.
        QJsonObject payload;
        Message message;
        reportsA.sendLocalMessage(direct, QStringLiteral("Офлайн-привет"),
                                  {}, payload, message);
        netA.sendMessage(message.id, payload, {QStringLiteral("id-b2")});

        waitFor([&] { return false; }, 400);
        check(reportsB.loadOne(direct).messages.isEmpty(),
              QStringLiteral("без получателя сообщение ещё не доставлено"));

        PeerInfo peerA;
        peerA.id = QStringLiteral("id-a2");
        peerA.name = QStringLiteral("Алиса");
        peerA.address = QStringLiteral("127.0.0.1");
        peerA.port = tcpA;
        peerA.online = true;

        PeerInfo peerB;
        peerB.id = QStringLiteral("id-b2");
        peerB.name = QStringLiteral("Борис");
        peerB.address = QStringLiteral("127.0.0.1");
        peerB.port = tcpB;
        peerB.online = true;

        netA.injectPeerForTesting(peerB);
        netB.injectPeerForTesting(peerA);

        const bool delivered = waitFor([&] {
            for (const Message& m : reportsB.loadOne(direct).messages) {
                if (m.text == QStringLiteral("Офлайн-привет"))
                    return true;
            }
            return false;
        }, 10000);
        check(delivered, QStringLiteral("сообщение доставлено после появления получателя"));

        netB.stop();
        netA.stop();
    }

    QDir(root).removeRecursively();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SupportILSelfTest"));

    std::printf("Support IL self-test\n");
    testStorage();
    testNetwork();
    testDiscovery();
    testOfflineQueue();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }

    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return 1;
}
