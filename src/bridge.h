#pragma once

#include "common.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;

// ---------------------------------------------------------------------------
// Localhost bridge used by the Chrome extension.
//
// The extension POSTs a JSON download description to 127.0.0.1 on the first
// free port in the configured range. Binding to loopback and accepting only
// chrome-extension:// origins keeps ordinary web pages from injecting
// downloads into the app.
// ---------------------------------------------------------------------------
class ChromeBridge : public QObject {
public:
    explicit ChromeBridge(QObject* parent = nullptr);
    ~ChromeBridge() override;

    ChromeBridge(const ChromeBridge&) = delete;
    ChromeBridge& operator=(const ChromeBridge&) = delete;

    bool start(quint16 firstPort, int attempts, QString* error);
    void stop();

    void setDownloadHandler(
        std::function<void(ChromeDownloadRequest)> handler);

    quint16 port() const { return port_; }

private:
    void acceptConnections();

    QTcpServer* server_ = nullptr;
    std::function<void(ChromeDownloadRequest)> handler_;
    quint16 port_ = 0;
};
