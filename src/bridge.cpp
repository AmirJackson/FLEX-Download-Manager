#include "bridge.h"
#include "ytdlp.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMap>
#include <QPointer>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

constexpr int kMaxRequestBytes = 256 * 1024;
constexpr int kExtractTimeoutMs = 45000;

QByteArray JsonError(const char* reason) {
    QJsonObject object;
    object.insert(QStringLiteral("ok"), false);
    object.insert(QStringLiteral("reason"), QString::fromLatin1(reason));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// Turns the yt-dlp metadata dump into the compact quality list the media panel
// shows. One entry per height keeps the menu short even when YouTube offers a
// dozen codec variants per resolution.
QByteArray BuildExtraction(const QByteArray& raw) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return JsonError("parse");
    }

    const QJsonObject info = document.object();
    if (info.value(QStringLiteral("_type")).toString() ==
        QStringLiteral("playlist")) {
        return JsonError("playlist");
    }

    struct Pick {
        QJsonObject format;
        bool progressive = false;
        double score = -1;
    };

    struct AudioPick {
        QJsonObject format;
        QString ext;
        int bitrate = 0;
        double size = 0;
        double score = -1e9;
    };

    QMap<int, Pick> byHeight;
    QMap<QPair<int, QString>, AudioPick> byAudio;
    double audioSize = 0;
    double audioScore = -1e9;

    // yt-dlp leaves filesize empty for some encodings, so fall back to the
    // bitrate and duration the same JSON reports. Approximate, but far better
    // than showing only the audio track's size.
    const double duration = info.value(QStringLiteral("duration")).toDouble(0);
    const auto estimateSize = [duration](const QJsonObject& format) {
        const double exact =
            format.value(QStringLiteral("filesize")).toDouble(
                format.value(QStringLiteral("filesize_approx")).toDouble(0));
        if (exact > 0) return exact;

        double rate = format.value(QStringLiteral("tbr")).toDouble(0);
        if (rate <= 0) rate = format.value(QStringLiteral("abr")).toDouble(0);
        if (rate <= 0 || duration <= 0) return 0.0;
        return rate * 1000.0 / 8.0 * duration;
    };

    const QJsonArray formats = info.value(QStringLiteral("formats")).toArray();
    for (const QJsonValue& value : formats) {
        const QJsonObject format = value.toObject();
        const QString vcodec =
            format.value(QStringLiteral("vcodec")).toString(QStringLiteral("none"));
        const QString acodec =
            format.value(QStringLiteral("acodec")).toString(QStringLiteral("none"));
        const double size = estimateSize(format);

        if (vcodec != QStringLiteral("none")) {
            const int height =
                (int)format.value(QStringLiteral("height")).toDouble(0);
            if (height <= 0) continue;

            double score = format.value(QStringLiteral("tbr")).toDouble(0);
            if (format.value(QStringLiteral("ext")).toString() ==
                QStringLiteral("mp4")) {
                score += 100;
            }
            if (size > 0) score += 1;
            if (acodec != QStringLiteral("none")) score += 1000;

            Pick& pick = byHeight[height];
            if (score > pick.score) {
                pick.score = score;
                pick.format = format;
            }
            if (acodec != QStringLiteral("none")) pick.progressive = true;
        } else if (acodec != QStringLiteral("none")) {
            QString ext = format.value(QStringLiteral("audio_ext")).toString();
            if (ext.isEmpty() || ext == QStringLiteral("none")) {
                ext = format.value(QStringLiteral("ext")).toString();
            }
            ext = ext.toLower();
            if (ext == QStringLiteral("mhtml")) continue;
            if (ext.isEmpty() || ext == QStringLiteral("none") ||
                ext == QStringLiteral("mp4")) {
                ext = QStringLiteral("m4a");
            }

            double rawAbr = format.value(QStringLiteral("abr")).toDouble(0);
            if (rawAbr <= 0) {
                rawAbr = format.value(QStringLiteral("tbr")).toDouble(0);
            }
            const int bitrate =
                rawAbr > 0 ? std::max(8, qRound(rawAbr / 8.0) * 8) : 0;

            const QString formatId =
                format.value(QStringLiteral("format_id")).toString();
            const QString protocol =
                format.value(QStringLiteral("protocol")).toString();
            const bool isDrc =
                formatId.contains(QStringLiteral("drc"), Qt::CaseInsensitive);
            const bool isDirect = (protocol == QStringLiteral("https") ||
                                   protocol == QStringLiteral("http"));
            const double langPref =
                format.value(QStringLiteral("language_preference")).toDouble(-1);

            const double mergeScore =
                rawAbr + (ext == QStringLiteral("m4a") ? 10.0 : 0.0) +
                (isDrc ? 0.0 : 500.0) + langPref * 100.0;
            if (mergeScore > audioScore) {
                audioScore = mergeScore;
                audioSize = size;
            }

            double pickScore = rawAbr;
            if (size > 0) pickScore += 10.0;
            if (isDirect) pickScore += 500.0;
            if (!isDrc) pickScore += 1000.0;
            pickScore += langPref * 100.0;
            pickScore += format.value(QStringLiteral("quality")).toDouble(0) * 10.0;

            AudioPick& pick = byAudio[qMakePair(bitrate, ext)];
            if (pickScore > pick.score) {
                pick.score = pickScore;
                pick.format = format;
                pick.ext = ext;
                pick.bitrate = bitrate;
                pick.size = size;
            }
        }
    }

    QJsonArray list;
    for (auto it = byHeight.constEnd(); it != byHeight.constBegin();) {
        --it;
        const int height = it.key();
        const Pick& pick = it.value();
        const QJsonObject& format = pick.format;

        const double fps = format.value(QStringLiteral("fps")).toDouble(0);
        QString label = QString::number(height) + QStringLiteral("p");
        if (fps >= 50) label += QString::number(qRound(fps));

        const double size = estimateSize(format);

        QJsonObject entry;
        entry.insert(QStringLiteral("height"), height);
        entry.insert(QStringLiteral("label"), label);
        entry.insert(QStringLiteral("kind"), QStringLiteral("video"));
        entry.insert(QStringLiteral("ext"),
                     pick.progressive
                         ? format.value(QStringLiteral("ext")).toString(
                               QStringLiteral("mp4"))
                         : QStringLiteral("mp4"));
        entry.insert(QStringLiteral("merge"), !pick.progressive);
        entry.insert(QStringLiteral("size"),
                     (double)(size + (pick.progressive ? 0 : audioSize)));
        entry.insert(
            QStringLiteral("id"),
            pick.progressive
                ? QStringLiteral("best[height=%1][acodec!=none]").arg(height)
                : QStringLiteral("bv*[height=%1][ext=mp4]+ba[ext=m4a]/"
                                 "bv*[height=%1]+ba/b[height=%1]")
                      .arg(height));
        list.append(entry);
    }

    std::vector<AudioPick> audioList;
    audioList.reserve((size_t)byAudio.size());
    bool hasBitrateAudio = false;
    for (auto it = byAudio.constBegin(); it != byAudio.constEnd(); ++it) {
        if (it.value().bitrate > 0) hasBitrateAudio = true;
    }
    for (auto it = byAudio.constBegin(); it != byAudio.constEnd(); ++it) {
        if (hasBitrateAudio && it.value().bitrate == 0) continue;
        audioList.push_back(it.value());
    }
    auto extRank = [](const QString& ext) {
        if (ext == QStringLiteral("m4a")) return 0;
        if (ext == QStringLiteral("mp3")) return 1;
        if (ext == QStringLiteral("aac")) return 2;
        if (ext == QStringLiteral("opus")) return 3;
        if (ext == QStringLiteral("ogg")) return 4;
        if (ext == QStringLiteral("webm")) return 5;
        return 6;
    };
    std::sort(audioList.begin(), audioList.end(),
              [&extRank](const AudioPick& a, const AudioPick& b) {
                  if (a.bitrate != b.bitrate) return a.bitrate > b.bitrate;
                  const int rankA = extRank(a.ext);
                  const int rankB = extRank(b.ext);
                  if (rankA != rankB) return rankA < rankB;
                  if (a.size != b.size) return a.size > b.size;
                  return a.ext < b.ext;
              });

    for (const AudioPick& pick : audioList) {
        const QString formatId =
            pick.format.value(QStringLiteral("format_id")).toString();
        bool simpleId = !formatId.isEmpty();
        for (QChar ch : formatId) {
            if (!ch.isLetterOrNumber() && ch != QLatin1Char('_') &&
                ch != QLatin1Char('-')) {
                simpleId = false;
                break;
            }
        }
        const QString selector =
            simpleId
                ? QStringLiteral("%1/bestaudio[ext=%2]/bestaudio")
                      .arg(formatId, pick.ext)
                : QStringLiteral("bestaudio[ext=%1]/bestaudio").arg(pick.ext);

        const QString extUpper = pick.ext.toUpper();
        const QString label =
            pick.bitrate > 0
                ? QStringLiteral("%1 %2 kbps").arg(extUpper).arg(pick.bitrate)
                : extUpper;

        QJsonObject entry;
        entry.insert(QStringLiteral("height"), 0);
        entry.insert(QStringLiteral("bitrate"), pick.bitrate);
        entry.insert(QStringLiteral("label"), label);
        entry.insert(QStringLiteral("kind"), QStringLiteral("audio"));
        entry.insert(QStringLiteral("ext"), pick.ext);
        entry.insert(QStringLiteral("merge"), false);
        entry.insert(QStringLiteral("size"), pick.size);
        entry.insert(QStringLiteral("id"), selector);
        list.append(entry);
    }

    if (list.isEmpty()) {
        // SoundCloud and similar audio-only pages.
        QJsonObject entry;
        entry.insert(QStringLiteral("height"), 0);
        entry.insert(QStringLiteral("bitrate"), 0);
        entry.insert(QStringLiteral("label"), QStringLiteral("Audio"));
        entry.insert(QStringLiteral("kind"), QStringLiteral("audio"));
        entry.insert(QStringLiteral("ext"), QStringLiteral("m4a"));
        entry.insert(QStringLiteral("merge"), false);
        entry.insert(QStringLiteral("size"), audioSize);
        entry.insert(QStringLiteral("id"),
                     QStringLiteral("bestaudio[ext=m4a]/bestaudio"));
        list.append(entry);
    }

    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("title"),
                  info.value(QStringLiteral("title")).toString());
    result.insert(QStringLiteral("duration"),
                  info.value(QStringLiteral("duration")).toDouble(0));
    result.insert(QStringLiteral("pageUrl"),
                  info.value(QStringLiteral("webpage_url")).toString());
    result.insert(QStringLiteral("ffmpeg"), !FindFfmpeg().empty());
    result.insert(QStringLiteral("formats"), list);
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

const char* ReasonPhrase(int code) {
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 503: return "Service Unavailable";
        default:  return "Error";
    }
}

void SendResponse(QTcpSocket* socket, int code, const QByteArray& body) {
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(code) + " " +
                ReasonPhrase(code) + "\r\n";
    response += "Content-Type: application/json; charset=utf-8\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response +=
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    response += "Access-Control-Allow-Private-Network: true\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;

    socket->write(response);
    socket->disconnectFromHost();
}

struct SocketState {
    QByteArray buffer;
    bool handled = false;
};

struct ParsedRequest {
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
    bool valid = false;
};

bool ParseHeaders(const QByteArray& block, ParsedRequest& request) {
    const QList<QByteArray> lines = block.split('\n');
    if (lines.isEmpty()) return false;

    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() < 2) return false;
    request.method = first[0].trimmed().toUpper();
    request.path = first[1].trimmed();
    if (request.method.isEmpty() || request.path.isEmpty()) return false;

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        request.headers.insert(name, value);
    }
    request.valid = true;
    return true;
}

}  // namespace

ChromeBridge::ChromeBridge(QObject* parent) : QObject(parent) {}

ChromeBridge::~ChromeBridge() {
    stop();
}

bool ChromeBridge::start(quint16 firstPort, int attempts, QString* error) {
    stop();

    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this,
            [this] { acceptConnections(); });

    const int count = attempts > 0 ? attempts : 1;
    for (int i = 0; i < count; ++i) {
        const int candidate = (int)firstPort + i;
        if (candidate <= 0 || candidate > 65535) break;
        if (server_->listen(QHostAddress::LocalHost, (quint16)candidate)) {
            port_ = (quint16)candidate;
            return true;
        }
    }

    if (error) *error = server_->errorString();
    delete server_;
    server_ = nullptr;
    port_ = 0;
    return false;
}

void ChromeBridge::stop() {
    port_ = 0;
    if (!server_) return;
    delete server_;
    server_ = nullptr;
}

void ChromeBridge::setDownloadHandler(
    std::function<void(ChromeDownloadRequest)> handler) {
    handler_ = std::move(handler);
}

void ChromeBridge::acceptConnections() {
    if (!server_) return;
    while (server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        if (!socket) continue;

        auto state = std::make_shared<SocketState>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, state] {
            if (state->handled) {
                socket->readAll();
                return;
            }

            state->buffer += socket->readAll();
            if (state->buffer.size() > kMaxRequestBytes) {
                state->handled = true;
                SendResponse(socket, 413, "{\"ok\":false}");
                return;
            }

            const int headerEnd = state->buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) return;

            ParsedRequest request;
            if (!ParseHeaders(state->buffer.left(headerEnd), request)) {
                state->handled = true;
                SendResponse(socket, 400, "{\"ok\":false}");
                return;
            }

            bool lengthOk = false;
            const int contentLength =
                request.headers.value("content-length").toInt(&lengthOk);
            if (!lengthOk) {
                request.headers.remove("content-length");
            }
            if (contentLength < 0 || contentLength > kMaxRequestBytes) {
                state->handled = true;
                SendResponse(socket, 413, "{\"ok\":false}");
                return;
            }

            const int bodyStart = headerEnd + 4;
            if (state->buffer.size() < bodyStart + contentLength) return;
            request.body = state->buffer.mid(bodyStart, contentLength);
            state->buffer.clear();
            state->handled = true;

            const QByteArray origin = request.headers.value("origin");
            if (!origin.isEmpty() &&
                !origin.startsWith("chrome-extension://")) {
                SendResponse(socket, 403,
                             "{\"ok\":false,\"error\":\"origin\"}");
                return;
            }

            if (request.method == "OPTIONS") {
                SendResponse(socket, 204, QByteArray());
                return;
            }

            if (request.method == "GET" && request.path == "/ping") {
                SendResponse(
                    socket, 200,
                    "{\"ok\":true,\"app\":\"FLEX Download Manager\",\"version\":1}");
                return;
            }

            if (request.method != "POST" ||
                (request.path != "/download" && request.path != "/extract")) {
                SendResponse(socket, 404, "{\"ok\":false}");
                return;
            }

            QJsonParseError parseError;
            const QJsonDocument document =
                QJsonDocument::fromJson(request.body, &parseError);
            if (parseError.error != QJsonParseError::NoError ||
                !document.isObject()) {
                SendResponse(socket, 400, "{\"ok\":false}");
                return;
            }

            const QJsonObject object = document.object();

            // Quality extraction for YouTube and the other sites yt-dlp knows.
            // yt-dlp can take several seconds, so the reply is sent from the
            // process callbacks instead of blocking this socket handler.
            if (request.path == "/extract") {
                const QString pageUrl =
                    object.value(QStringLiteral("url")).toString();
                if (!pageUrl.startsWith(QStringLiteral("http://")) &&
                    !pageUrl.startsWith(QStringLiteral("https://"))) {
                    SendResponse(socket, 400, JsonError("url"));
                    return;
                }
                if (pageUrl.size() > 4096) {
                    SendResponse(socket, 413, JsonError("url"));
                    return;
                }

                const std::wstring tool = FindYtDlp();
                if (tool.empty()) {
                    SendResponse(socket, 200, JsonError("no-yt-dlp"));
                    return;
                }

                QPointer<QTcpSocket> target(socket);
                auto* process = new QProcess(this);
                auto* timer = new QTimer(process);
                auto finished = std::make_shared<bool>(false);
                timer->setSingleShot(true);

                auto reply = [target, process, timer, finished](
                                 const QByteArray& body) {
                    if (*finished) return;
                    *finished = true;
                    timer->stop();
                    if (target) SendResponse(target.data(), 200, body);
                    process->deleteLater();
                };

                connect(process, &QProcess::finished, this,
                        [process, reply](int code, QProcess::ExitStatus status) {
                            if (status != QProcess::NormalExit || code != 0) {
                                reply(JsonError("extract"));
                                return;
                            }
                            reply(BuildExtraction(
                                process->readAllStandardOutput()));
                        });
                connect(process, &QProcess::errorOccurred, this,
                        [reply](QProcess::ProcessError) {
                            reply(JsonError("extract"));
                        });
                connect(timer, &QTimer::timeout, this, [process, reply] {
                    process->kill();
                    reply(JsonError("timeout"));
                });

                process->setProgram(QString::fromStdWString(tool));
                process->setProcessChannelMode(QProcess::SeparateChannels);
                process->setArguments(
                    {QStringLiteral("-J"), QStringLiteral("--no-warnings"),
                     QStringLiteral("--no-playlist"),
                     QStringLiteral("--skip-download"),
                     QStringLiteral("--socket-timeout"),
                     QStringLiteral("20"), QStringLiteral("--"), pageUrl});
                timer->start(kExtractTimeoutMs);
                process->start();
                return;
            }

            ChromeDownloadRequest download;
            download.url = object.value("url").toString().toStdWString();
            download.fileName =
                object.value("filename").toString().toStdWString();
            download.referrer =
                object.value("referrer").toString().toStdWString();
            download.mime = object.value("mime").toString().toStdWString();
            download.description =
                object.value("description").toString().toStdWString();
            download.category =
                object.value("category").toString().toStdWString();
            download.formatId =
                object.value("formatId").toString().toStdWString();
            download.pageUrl =
                object.value("pageUrl").toString().toStdWString();
            download.merge = object.value("merge").toBool(false);

            const double total = object.value("totalBytes").toDouble(0.0);
            if (total > 0.0) {
                download.totalBytes = (uint64_t)total;
            }

            if (!download.formatId.empty()) {
                if (download.pageUrl.empty()) download.pageUrl = download.url;
                if (!ParseUrl(download.pageUrl).valid) {
                    SendResponse(socket, 400,
                                 "{\"ok\":false,\"error\":\"url\"}");
                    return;
                }
                download.url = download.pageUrl;
            } else if (!ParseUrl(download.url).valid) {
                SendResponse(socket, 400,
                             "{\"ok\":false,\"error\":\"url\"}");
                return;
            }

            if (download.category.empty()) {
                download.category =
                    InferCategory(download.fileName, download.mime);
            }

            if (!handler_) {
                SendResponse(socket, 503, "{\"ok\":false}");
                return;
            }

            SendResponse(socket, 200, "{\"ok\":true}");

            auto handler = handler_;
            QTimer::singleShot(
                0, this,
                [handler, download = std::move(download)]() mutable {
                    handler(std::move(download));
                });
        });

        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
    }
}
