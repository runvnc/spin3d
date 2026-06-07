#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <random>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>

static constexpr double PI = 3.14159265358979323846;

static QString appDataDir() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) {
        path = QDir::homePath() + "/.local/share/spin3d_cool";
    }
    QDir().mkpath(path);
    return path;
}

static QString historyPath() {
    return appDataDir() + "/history.jsonl";
}

static QString nowIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

static QString shortId() {
    return QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + "_" + QString::number(QRandomGenerator::global()->bounded(100000), 36);
}

static QString jsonValueToText(const QJsonValue &v) {
    if (v.isString()) return v.toString();
    if (v.isNull() || v.isUndefined()) return "";
    QJsonDocument doc;
    if (v.isObject()) doc = QJsonDocument(v.toObject());
    else if (v.isArray()) doc = QJsonDocument(v.toArray());
    else return v.toVariant().toString();
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

static QString elideMiddle(const QString &s, int maxLen) {
    QString t = s.simplified();
    if (t.size() <= maxLen) return t;
    return t.left(maxLen - 1) + QString::fromUtf8("…");
}

static QString agentNameSetting() {
    QSettings s("MindRoot", "spin3d_cool");
    QString agent = s.value("agentName", "Assistant").toString().trimmed();
    return agent.isEmpty() ? "Assistant" : agent;
}

static void setAgentNameSetting(const QString &name) {
    QSettings s("MindRoot", "spin3d_cool");
    QString n = name.trimmed();
    if (n.isEmpty()) n = "Assistant";
    s.setValue("agentName", n);
}

static QString positionSetting() {
    QSettings s("MindRoot", "spin3d_cool");
    QString pos = s.value("position", "bottom-left").toString();
    if (pos.isEmpty()) pos = "bottom-left";
    return pos;
}

static void setPositionSetting(const QString &pos) {
    QSettings s("MindRoot", "spin3d_cool");
    s.setValue("position", pos.isEmpty() ? "bottom-left" : pos);
}

static int offsetXSetting() {
    QSettings s("MindRoot", "spin3d_cool");
    return s.value("offsetX", -10).toInt();
}

static int offsetYSetting() {
    QSettings s("MindRoot", "spin3d_cool");
    return s.value("offsetY", -26).toInt();
}

static void setOffsetSettings(int x, int y) {
    QSettings s("MindRoot", "spin3d_cool");
    s.setValue("offsetX", x);
    s.setValue("offsetY", y);
}


static int timeoutSetting() {
    QSettings s("MindRoot", "spin3d_cool");
    return s.value("timeoutMs", 600000).toInt();
}

static void setTimeoutSetting(int ms) {
    QSettings s("MindRoot", "spin3d_cool");
    s.setValue("timeoutMs", ms);
}

struct HistoryEntry {
    QString id;
    QString threadId;
    QString continuedFrom;
    QString createdAt;
    QString agentName;
    QString query;
    QString response;
    QString status;
    QString logId;

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["threadId"] = threadId;
        o["continuedFrom"] = continuedFrom;
        o["createdAt"] = createdAt;
        o["agentName"] = agentName;
        o["query"] = query;
        o["response"] = response;
        o["status"] = status;
        o["logId"] = logId;
        return o;
    }

    static HistoryEntry fromJson(const QJsonObject &o) {
        HistoryEntry e;
        e.id = o["id"].toString();
        e.threadId = o["threadId"].toString();
        e.continuedFrom = o["continuedFrom"].toString();
        e.createdAt = o["createdAt"].toString();
        e.agentName = o["agentName"].toString();
        e.query = o["query"].toString();
        e.response = o["response"].toString();
        e.status = o["status"].toString();
        e.logId = o["logId"].toString();
        return e;
    }
};

static void appendHistory(const HistoryEntry &e) {
    QFile f(historyPath());
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write(QJsonDocument(e.toJson()).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.close();
}

static std::vector<HistoryEntry> loadHistory(int limit = 100) {
    std::vector<HistoryEntry> out;
    QFile f(historyPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;

    while (!f.atEnd()) {
        QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            out.push_back(HistoryEntry::fromJson(doc.object()));
        }
    }
    f.close();

    if ((int)out.size() > limit) {
        out.erase(out.begin(), out.end() - limit);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

static std::optional<HistoryEntry> latestHistoryEntry() {
    auto h = loadHistory(1);
    if (h.empty()) return std::nullopt;
    return h.front();
}

static QString continuationPrompt(const HistoryEntry &prev, const QString &newQuery) {
    QString prompt;
    prompt += "Continue from this previous desktop-agent exchange.\n\n";
    prompt += "Previous user request:\n";
    prompt += prev.query + "\n\n";
    prompt += "Previous assistant response:\n";
    prompt += prev.response + "\n\n";
    prompt += "New user request:\n";
    prompt += newQuery + "\n\n";
    prompt += "Respond to the new request, using the previous exchange only as relevant context.";
    return prompt;
}

class MarkdownBrowserCool : public QTextBrowser {
    Q_OBJECT
    QNetworkAccessManager *net;
public:
    MarkdownBrowserCool(QWidget *parent = nullptr) : QTextBrowser(parent) {
        net = new QNetworkAccessManager(this);
    }

protected:
    QVariant loadResource(int type, const QUrl &name) override {
        if (type == QTextDocument::ImageResource && (name.scheme() == "http" || name.scheme() == "https")) {
            QNetworkReply *reply = net->get(QNetworkRequest(name));
            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                QPixmap pix;
                if (pix.loadFromData(data)) {
                    reply->deleteLater();
                    return pix;
                }
            }
            reply->deleteLater();
        }
        return QTextBrowser::loadResource(type, name);
    }
};

static QString panelStyle(const QString &className) {
    return className + " {"
        "  background: rgba(5, 9, 22, 225);"
        "  color: #E9FFFF;"
        "  border: 1px solid rgba(64, 240, 255, 150);"
        "  border-radius: 18px;"
        "  padding: 12px;"
        "  font-size: 14px;"
        "  font-family: 'Inter', 'Segoe UI', sans-serif;"
        "}";
}

static QString buttonStyle() {
    return
        "QPushButton {"
        "  background: rgba(0, 240, 255, 34);"
        "  color: rgba(230,255,255,220);"
        "  border: 1px solid rgba(0, 240, 255, 90);"
        "  border-radius: 10px;"
        "  padding: 6px 10px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(255, 166, 48, 42);"
        "  border: 1px solid rgba(255, 166, 48, 125);"
        "}";
}

class NeonCommandPanel : public QWidget {
    Q_OBJECT
    QLineEdit *lineEdit;
    MarkdownBrowserCool *resultBrowser;
    QWidget *footer;
    QLabel *queryLabel;
    QPushButton *copyButton;
    QPushButton *pinButton;
    QLineEdit *followEdit;
    QPushButton *followSendButton;
    QPushButton *closeButton;
    QLabel *modeLabel;
    QNetworkAccessManager *net;
    std::atomic<bool> requestInFlight{false};
    bool pinned = false;

    std::function<void(bool)> processingCallback;
    std::optional<HistoryEntry> followContext;
    std::optional<HistoryEntry> currentEntry;

public:
    std::function<void()> onMouseEnter;
    std::function<void()> onMouseLeave;
    std::function<void()> onDismiss;
    std::function<void(const HistoryEntry&)> onHistoryAdded;
    std::function<void()> onOpenHistory;
    std::function<void()> onOpenSettings;

    NeonCommandPanel(std::function<void(bool)> cb, QWidget *parent = nullptr)
        : QWidget(parent), processingCallback(cb) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(560, 92);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(22, 16, 22, 16);
        layout->setSpacing(10);

        modeLabel = new QLabel(this);
        modeLabel->setStyleSheet("QLabel { color: rgba(180,255,255,150); font-size: 11px; padding-left: 8px; }");
        modeLabel->hide();
        layout->addWidget(modeLabel);

        queryLabel = new QLabel(this);
        queryLabel->setWordWrap(true);
        queryLabel->setStyleSheet(
            "QLabel { color: rgba(255, 200, 100, 200); font-size: 14px; padding: 4px 10px; font-style: italic; }"
        );
        queryLabel->hide();
        layout->addWidget(queryLabel);

        lineEdit = new QLineEdit(this);
        lineEdit->setPlaceholderText("Ask the agent...");
        lineEdit->setCursorPosition(0);
        lineEdit->setMinimumHeight(48);
        lineEdit->setStyleSheet(
            "QLineEdit {"
            "  background: rgba(7, 12, 28, 205);"
            "  color: #E9FFFF;"
            "  selection-background-color: rgba(0, 240, 255, 120);"
            "  border: 1px solid rgba(64, 240, 255, 180);"
            "  border-radius: 18px;"
            "  padding: 10px 18px;"
            "  font-size: 18px;"
            "  font-family: 'Inter', 'Segoe UI', sans-serif;"
            "}"
            "QLineEdit:focus {"
            "  background: rgba(9, 18, 42, 230);"
            "  border: 1px solid rgba(255, 166, 48, 210);"
            "}"
            "QLineEdit::placeholder { color: rgba(190, 245, 255, 110); }"
        );
        layout->addWidget(lineEdit);

        resultBrowser = new MarkdownBrowserCool(this);
        resultBrowser->setOpenExternalLinks(true);
        resultBrowser->setStyleSheet(
            "MarkdownBrowserCool {"
            "  background: rgba(5, 9, 22, 225);"
            "  color: #E9FFFF;"
            "  border: 1px solid rgba(64, 240, 255, 150);"
            "  border-radius: 18px;"
            "  padding: 16px 18px;"
            "  font-size: 15px;"
            "  font-family: 'Inter', 'Segoe UI', sans-serif;"
            "}"
            "QScrollBar:vertical { background: rgba(255,255,255,25); width: 8px; border-radius: 4px; }"
            "QScrollBar::handle:vertical { background: rgba(0,240,255,150); border-radius: 4px; }"
        );
        resultBrowser->hide();
        layout->addWidget(resultBrowser);

        auto *followRow = new QWidget(this);
        auto *followLayout = new QHBoxLayout(followRow);
        followLayout->setContentsMargins(4, 0, 4, 0);
        followLayout->setSpacing(8);
        followEdit = new QLineEdit(followRow);
        followEdit->setPlaceholderText("Ask a follow-up about this response...");
        followEdit->setMinimumHeight(38);
        followEdit->setStyleSheet(
            "QLineEdit {"
            "  background: rgba(7, 12, 28, 205);"
            "  color: #E9FFFF;"
            "  selection-background-color: rgba(0, 240, 255, 100);"
            "  border: 1px solid rgba(64, 240, 255, 130);"
            "  border-radius: 14px;"
            "  padding: 8px 13px;"
            "  font-size: 14px;"
            "  font-family: 'Inter', 'Segoe UI', sans-serif;"
            "}"
            "QLineEdit:focus { border: 1px solid rgba(255,166,48,170); }"
        );
        followSendButton = new QPushButton(">", followRow);
        followSendButton->setFixedWidth(42);
        followSendButton->setMinimumHeight(38);
        followSendButton->setToolTip("Send follow-up");
        followSendButton->setStyleSheet(buttonStyle());
        followLayout->addWidget(followEdit, 1);
        followLayout->addWidget(followSendButton);
        followRow->hide();
        followRow->setObjectName("followRow");
        layout->addWidget(followRow);

        footer = new QWidget(this);
        auto *footerLayout = new QHBoxLayout(footer);
        footerLayout->setContentsMargins(4, 0, 4, 0);
        footerLayout->addStretch();

        copyButton = new QPushButton("copy", footer);
        pinButton = new QPushButton("pin", footer);
        QPushButton *historyButton = new QPushButton("history", footer);
        QPushButton *settingsButton = new QPushButton("settings", footer);
        closeButton = new QPushButton("close", footer);
        for (auto *b : {copyButton, pinButton, historyButton, settingsButton, closeButton}) {
            b->setStyleSheet(buttonStyle());
            footerLayout->addWidget(b);
        }
        footer->hide();
        layout->addWidget(footer);

        auto *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(42);
        shadow->setColor(QColor(0, 230, 255, 180));
        shadow->setOffset(0, 0);
        lineEdit->setGraphicsEffect(shadow);

        setFocusPolicy(Qt::StrongFocus);
        net = new QNetworkAccessManager(this);

        connect(copyButton, &QPushButton::clicked, this, [this]() {
            if (currentEntry) QApplication::clipboard()->setText(currentEntry->response);
        });
        auto submitInlineFollow = [this]() {
            if (!currentEntry) return;
            QString text = followEdit->text().trimmed();
            if (text.isEmpty()) return;
            setFollowContext(*currentEntry);
            lineEdit->setText(text);
            submitLine();
        };
        connect(followEdit, &QLineEdit::returnPressed, this, submitInlineFollow);
        connect(followSendButton, &QPushButton::clicked, this, submitInlineFollow);
        connect(pinButton, &QPushButton::clicked, this, [this]() {
            pinned = !pinned;
            pinButton->setText(pinned ? "unpin" : "pin");
        });
        connect(closeButton, &QPushButton::clicked, this, [this]() {
            pinned = false;
            close();
            deleteLater();
            if (onDismiss) onDismiss();
        });
        connect(historyButton, &QPushButton::clicked, this, [this]() {
            if (onOpenHistory) onOpenHistory();
        });
        connect(settingsButton, &QPushButton::clicked, this, [this]() {
            if (onOpenSettings) onOpenSettings();
        });

        connect(lineEdit, &QLineEdit::returnPressed, this, [this]() {
            submitLine();
        });
    }

    bool isRequestInFlight() const { return requestInFlight; }
    bool isShowingResult() const { return resultBrowser && resultBrowser->isVisible(); }
    bool isPinned() const { return pinned; }

    void setFollowContext(const HistoryEntry &e) {
        followContext = e;
        modeLabel->setText(QString("following up on: %1").arg(elideMiddle(e.query, 64)));
        modeLabel->show();
        lineEdit->setPlaceholderText("Follow up on that result...");
    }

    void clearFollowContext() {
        followContext.reset();
        modeLabel->hide();
        lineEdit->setPlaceholderText("Ask the agent...");
    }

    void showInput() {
        lineEdit->show();
        resultBrowser->hide();
        queryLabel->hide();
        if (auto *fr = findChild<QWidget*>("followRow")) fr->hide();
        footer->hide();
        pinned = false;
        pinButton->setText("pin");
        setFixedSize(560, followContext ? 112 : 92);
        show();
        raise();
        activateWindow();
        lineEdit->setFocus();
    }

    void showEntry(const HistoryEntry &e) {
        currentEntry = e;
        queryLabel->setText("> " + e.query);
        queryLabel->show();
        resultBrowser->setMarkdown(e.response);
        resultBrowser->document()->adjustSize();
        queryLabel->adjustSize();
        int qh = queryLabel->sizeHint().height() + 8;
        int dh = std::min(460 - qh, (int)resultBrowser->document()->size().height() + 52);
        setFixedSize(560, 158 + qh + dh);
        lineEdit->hide();
        modeLabel->setText(QString("%1  ·  %2").arg(e.agentName.isEmpty() ? "Assistant" : e.agentName, e.createdAt.left(19)));
        modeLabel->show();
        resultBrowser->show();
        if (auto *fr = findChild<QWidget*>("followRow")) { followEdit->clear(); fr->show(); }
        footer->show();
        show();
        raise();
        activateWindow();
        update();
        repaint();
    }

    void showLastResponse() {
        if (auto last = latestHistoryEntry()) {
            showEntry(*last);
        } else {
            showInput();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        QRectF r = rect().adjusted(7, 7, -7, -7);
        QPainterPath path;
        path.addRoundedRect(r, 24, 24);

        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(18, 30, 70, 170));
        bg.setColorAt(0.45, QColor(4, 8, 20, 215));
        bg.setColorAt(1.0, QColor(34, 28, 18, 165));
        p.fillPath(path, bg);

        QPen glow(QColor(0, 240, 255, 95), 2.0);
        p.setPen(glow);
        p.drawPath(path);

        QPen hot(QColor(255, 166, 48, 66), 1.0);
        p.setPen(hot);
        p.drawRoundedRect(r.adjusted(5, 5, -5, -5), 19, 19);
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            if (followContext && lineEdit->isVisible()) {
                clearFollowContext();
                e->accept();
                return;
            }
            if (onDismiss) onDismiss();
            close();
            deleteLater();
            return;
        }
        if (e->modifiers() & Qt::ControlModifier) {
            if (e->key() == Qt::Key_H && onOpenHistory) {
                onOpenHistory();
                return;
            }
            if (e->key() == Qt::Key_Comma && onOpenSettings) {
                onOpenSettings();
                return;
            }
            if (e->key() == Qt::Key_N) {
                clearFollowContext();
                return;
            }
        }
        QWidget::keyPressEvent(e);
    }

    void enterEvent(QEnterEvent *) override {
        if (onMouseEnter) onMouseEnter();
    }

    void leaveEvent(QEvent *) override {
        if (onMouseLeave) onMouseLeave();
    }

private:
    QString configuredAgentAndMaybeStrip(QString &text) {
        QString agent = agentNameSetting();
        if (text.startsWith("@")) {
            int sp = text.indexOf(' ');
            if (sp > 1) {
                agent = text.mid(1, sp - 1).trimmed();
                text = text.mid(sp + 1).trimmed();
            }
        }
        return agent.isEmpty() ? "Assistant" : agent;
    }

    void submitLine() {
        QString visibleText = lineEdit->text().trimmed();
        if (visibleText.isEmpty()) return;

        bool forceNew = false;
        bool forceContinue = false;

        if (visibleText.startsWith("/new ")) {
            visibleText = visibleText.mid(5).trimmed();
            forceNew = true;
        } else if (visibleText == "/new") {
            clearFollowContext();
            return;
        } else if (visibleText.startsWith("/continue ")) {
            visibleText = visibleText.mid(10).trimmed();
            forceContinue = true;
        } else if (visibleText == "/history") {
            if (onOpenHistory) onOpenHistory();
            return;
        } else if (visibleText == "/settings") {
            if (onOpenSettings) onOpenSettings();
            return;
        }

        QString agent = configuredAgentAndMaybeStrip(visibleText);
        if (visibleText.isEmpty()) return;

        std::optional<HistoryEntry> contextToUse;
        if (!forceNew && followContext) {
            contextToUse = followContext;
        } else if (forceContinue) {
            contextToUse = latestHistoryEntry();
        }

        QString instructions = visibleText;

        requestInFlight = true;
        resultBrowser->hide();
        footer->hide();
        setFixedSize(560, followContext ? 112 : 92);
        fprintf(stderr, "[spin3d_cool] Sending task to %s: %s\n", agent.toUtf8().constData(), visibleText.toUtf8().constData());
        if (processingCallback) processingCallback(true);

        lineEdit->clear();
        hide();
        lineEdit->setEnabled(false);

        auto env = QProcessEnvironment::systemEnvironment();
        QString port = env.value("MR_PORT", "8022");
        QUrl url(QString("http://localhost:%1/task/%2").arg(port, agent));
        QString apiKey = env.value("MR_KEY", "");
        if (!apiKey.isEmpty()) {
            QUrlQuery query;
            query.addQueryItem("api_key", apiKey);
            url.setQuery(query);
        }

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject body;
        body["instructions"] = instructions;
        if (contextToUse && !contextToUse->logId.isEmpty()) {
            body["log_id"] = contextToUse->logId;
        }
        QNetworkReply *reply = net->post(req, QJsonDocument(body).toJson());

        HistoryEntry pending;
        pending.id = shortId();
        pending.threadId = contextToUse ? (contextToUse->threadId.isEmpty() ? contextToUse->id : contextToUse->threadId) : ("thread_" + pending.id);
        pending.continuedFrom = contextToUse ? contextToUse->id : "";
        pending.createdAt = nowIso();
        pending.agentName = agent;
        pending.query = visibleText;

        QString cancelLogId = (contextToUse && !contextToUse->logId.isEmpty()) ? contextToUse->logId : QString();

        QTimer *timeout = new QTimer(this);
        timeout->setSingleShot(true);
        timeout->setInterval(timeoutSetting());
        connect(timeout, &QTimer::timeout, this, [this, reply, timeout, pending, cancelLogId, port, apiKey]() mutable {
            fprintf(stderr, "[spin3d_cool] TIMEOUT\n");
            if (processingCallback) processingCallback(false);
            lineEdit->setEnabled(true);
            requestInFlight = false;

            pending.status = "timeout";
            pending.response = "Timeout waiting for response.";
            appendHistory(pending);
            if (onHistoryAdded) onHistoryAdded(pending);
            showEntry(pending);

            // Try to cancel the server-side task
            if (!cancelLogId.isEmpty()) {
                QUrl cancelUrl(QString("http://localhost:%1/chat/%2/timeout_cancel/cancel").arg(port, cancelLogId));
                if (!apiKey.isEmpty()) {
                    QUrlQuery q;
                    q.addQueryItem("api_key", apiKey);
                    cancelUrl.setQuery(q);
                }
                QNetworkRequest cancelReq(cancelUrl);
                cancelReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                net->post(cancelReq, QByteArray());
            }

            reply->abort();
            reply->deleteLater();
            timeout->deleteLater();
        });
        timeout->start();

        connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, pending]() mutable {
            timeout->stop();
            timeout->deleteLater();

            QByteArray respData = reply->readAll();
            requestInFlight = false;
            if (processingCallback) processingCallback(false);
            lineEdit->setEnabled(true);

            pending.status = "ok";
            if (reply->error() != QNetworkReply::NoError) {
                pending.status = "error";
                pending.response = "Error: " + reply->errorString();
                appendHistory(pending);
                if (onHistoryAdded) onHistoryAdded(pending);
                showEntry(pending);
                reply->deleteLater();
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(respData);
            QJsonObject obj = doc.object();
            pending.logId = obj["log_id"].toString();

            if (obj["status"].toString() == "ok") {
                QString results = jsonValueToText(obj["result"]);
                if (results.isEmpty()) results = jsonValueToText(obj["results"]);
                if (results.isEmpty()) results = jsonValueToText(obj["output"]);
                if (results.isEmpty()) results = "Task completed but returned no output.";
                pending.response = results;
            } else {
                pending.status = "error";
                pending.response = "Unexpected response: " + QString::fromUtf8(respData.left(1000));
            }

            appendHistory(pending);
            if (onHistoryAdded) onHistoryAdded(pending);
            clearFollowContext();
            showEntry(pending);

            reply->deleteLater();
        });
    }
};

class SettingsPanel : public QWidget {
    Q_OBJECT
    QLineEdit *agentEdit;
    QComboBox *positionCombo;
    QSpinBox *offsetXSpin;
    QSpinBox *offsetYSpin;
    QSpinBox *timeoutSpin;

public:
    std::function<void()> onMouseEnter;
    std::function<void()> onMouseLeave;
    std::function<void()> onSaved;

    SettingsPanel(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(450, 420);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 22, 24, 22);
        layout->setSpacing(12);

        auto *title = new QLabel("Settings", this);
        title->setStyleSheet("QLabel { color: #E9FFFF; font-size: 20px; font-weight: 600; }");
        layout->addWidget(title);

        auto *label = new QLabel("Agent name for tasks", this);
        label->setStyleSheet("QLabel { color: rgba(210,255,255,170); font-size: 13px; }");
        layout->addWidget(label);

        agentEdit = new QLineEdit(this);
        agentEdit->setText(agentNameSetting());
        agentEdit->setPlaceholderText("Assistant");
        agentEdit->setStyleSheet(
            "QLineEdit { background: rgba(7,12,28,220); color: #E9FFFF; border: 1px solid rgba(64,240,255,150);"
            "border-radius: 14px; padding: 10px 14px; font-size: 16px; }"
            "QLineEdit:focus { border: 1px solid rgba(255,166,48,185); }"
        );
        layout->addWidget(agentEdit);

        auto *hint = new QLabel("Tip: prefix a task with @AgentName to override once.", this);
        hint->setStyleSheet("QLabel { color: rgba(190,245,255,105); font-size: 12px; }");
        layout->addWidget(hint);

        auto *posLabel = new QLabel("Position", this);
        posLabel->setStyleSheet("QLabel { color: rgba(210,255,255,170); font-size: 13px; }");
        layout->addWidget(posLabel);

        positionCombo = new QComboBox(this);
        positionCombo->addItem("Bottom left", "bottom-left");
        positionCombo->addItem("Bottom middle", "bottom-middle");
        positionCombo->addItem("Bottom right", "bottom-right");
        positionCombo->addItem("Top left", "top-left");
        positionCombo->addItem("Top middle", "top-middle");
        positionCombo->addItem("Top right", "top-right");
        int posIndex = positionCombo->findData(positionSetting());
        if (posIndex >= 0) positionCombo->setCurrentIndex(posIndex);
        positionCombo->setStyleSheet(
            "QComboBox { background: rgba(7,12,28,220); color: #E9FFFF; border: 1px solid rgba(64,240,255,150);"
            "border-radius: 12px; padding: 7px 10px; font-size: 14px; }"
            "QComboBox::drop-down { border: none; width: 24px; }"
            "QComboBox QAbstractItemView { background: rgba(5,9,22,245); color: #E9FFFF; selection-background-color: rgba(255,166,48,60); }"
        );
        layout->addWidget(positionCombo);

        auto *offsetLabel = new QLabel("Offset  (negative Y moves upward)", this);
        offsetLabel->setStyleSheet("QLabel { color: rgba(210,255,255,170); font-size: 13px; }");
        layout->addWidget(offsetLabel);

        auto *offsetRow = new QHBoxLayout();
        offsetXSpin = new QSpinBox(this);
        offsetYSpin = new QSpinBox(this);
        offsetXSpin->setRange(-1000, 1000);
        offsetYSpin->setRange(-1000, 1000);
        offsetXSpin->setValue(offsetXSetting());
        offsetYSpin->setValue(offsetYSetting());
        offsetXSpin->setPrefix("X ");
        offsetYSpin->setPrefix("Y ");
        QString spinStyle = "QSpinBox { background: rgba(7,12,28,220); color: #E9FFFF; border: 1px solid rgba(64,240,255,140); border-radius: 12px; padding: 7px 8px; font-size: 14px; }";
        offsetXSpin->setStyleSheet(spinStyle);
        offsetYSpin->setStyleSheet(spinStyle);
        offsetRow->addWidget(offsetXSpin);
        offsetRow->addWidget(offsetYSpin);
        layout->addLayout(offsetRow);

        auto *timeoutLabel = new QLabel("Timeout (seconds)", this);
        timeoutLabel->setStyleSheet("QLabel { color: rgba(210,255,255,170); font-size: 13px; }");
        layout->addWidget(timeoutLabel);

        timeoutSpin = new QSpinBox(this);
        timeoutSpin->setRange(30, 3600);
        timeoutSpin->setValue(timeoutSetting() / 1000);
        timeoutSpin->setSuffix(" s");
        timeoutSpin->setStyleSheet(spinStyle);
        layout->addWidget(timeoutSpin);

        auto *timeoutHint = new QLabel("How long to wait for agent response before cancelling.", this);
        timeoutHint->setStyleSheet("QLabel { color: rgba(190,245,255,105); font-size: 12px; }");
        layout->addWidget(timeoutHint);

        auto *row = new QHBoxLayout();
        row->addStretch();
        auto *save = new QPushButton("save", this);
        auto *closeButton = new QPushButton("close", this);
        save->setStyleSheet(buttonStyle());
        closeButton->setStyleSheet(buttonStyle());
        row->addWidget(save);
        row->addWidget(closeButton);
        layout->addLayout(row);

        connect(save, &QPushButton::clicked, this, [this]() {
            setAgentNameSetting(agentEdit->text());
            setPositionSetting(positionCombo->currentData().toString());
            setOffsetSettings(offsetXSpin->value(), offsetYSpin->value());
            setTimeoutSetting(timeoutSpin->value() * 1000);
            if (onSaved) onSaved();
            this->close();
        });
        connect(closeButton, &QPushButton::clicked, this, [this]() {
            this->close();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QRectF r = rect().adjusted(7, 7, -7, -7);
        QPainterPath path;
        path.addRoundedRect(r, 24, 24);
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(18, 30, 70, 190));
        bg.setColorAt(0.6, QColor(4, 8, 20, 230));
        bg.setColorAt(1.0, QColor(34, 28, 18, 182));
        p.fillPath(path, bg);
        p.setPen(QPen(QColor(0, 240, 255, 110), 1.5));
        p.drawPath(path);
    }

    void enterEvent(QEnterEvent *) override { if (onMouseEnter) onMouseEnter(); }
    void leaveEvent(QEvent *) override { if (onMouseLeave) onMouseLeave(); }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) close();
        QWidget::keyPressEvent(e);
    }
};

class HistoryPanel : public QWidget {
    Q_OBJECT
    QListWidget *list;
    std::vector<HistoryEntry> entries;

public:
    std::function<void()> onMouseEnter;
    std::function<void()> onMouseLeave;
    std::function<void(const HistoryEntry&)> onEntrySelected;

    HistoryPanel(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(520, 520);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(22, 20, 22, 20);
        layout->setSpacing(12);

        auto *topRow = new QHBoxLayout();
        auto *title = new QLabel("Recent Tasks", this);
        title->setStyleSheet("QLabel { color: #E9FFFF; font-size: 20px; font-weight: 600; }");
        auto *closeButton = new QPushButton("close", this);
        closeButton->setStyleSheet(buttonStyle());
        topRow->addWidget(title);
        topRow->addStretch();
        topRow->addWidget(closeButton);
        layout->addLayout(topRow);
        connect(closeButton, &QPushButton::clicked, this, [this]() { this->hide(); });

        list = new QListWidget(this);
        list->setStyleSheet(
            "QListWidget { background: rgba(5,9,22,180); color: #E9FFFF; border: 1px solid rgba(64,240,255,100);"
            "border-radius: 16px; padding: 8px; font-size: 13px; }"
            "QListWidget::item { padding: 10px; border-radius: 10px; }"
            "QListWidget::item:hover { background: rgba(0,240,255,28); }"
            "QListWidget::item:selected { background: rgba(255,166,48,38); }"
            "QScrollBar:vertical { background: rgba(255,255,255,20); width: 8px; border-radius: 4px; }"
            "QScrollBar::handle:vertical { background: rgba(0,240,255,130); border-radius: 4px; }"
        );
        layout->addWidget(list, 1);

        auto *hint = new QLabel("Click an item to reopen its response. Use follow-up from the response card to continue.", this);
        hint->setWordWrap(true);
        hint->setStyleSheet("QLabel { color: rgba(190,245,255,105); font-size: 12px; }");
        layout->addWidget(hint);

        refresh();

        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            QString id = item->data(Qt::UserRole).toString();
            for (const auto &e : entries) {
                if (e.id == id) {
                    if (onEntrySelected) onEntrySelected(e);
                    return;
                }
            }
        });
    }

    void refresh() {
        entries = loadHistory(100);
        list->clear();
        for (const auto &e : entries) {
            QString time = e.createdAt;
            if (time.size() >= 19) time = time.mid(11, 5);
            QString status = e.status == "ok" ? "" : (" [" + e.status + "]");
            QString text = QString("%1  %2%3\n%4")
                .arg(time, elideMiddle(e.query, 58), status, elideMiddle(e.response, 90));
            auto *item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, e.id);
            list->addItem(item);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QRectF r = rect().adjusted(7, 7, -7, -7);
        QPainterPath path;
        path.addRoundedRect(r, 24, 24);
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(18, 30, 70, 188));
        bg.setColorAt(0.55, QColor(4, 8, 20, 232));
        bg.setColorAt(1.0, QColor(34, 28, 18, 176));
        p.fillPath(path, bg);
        p.setPen(QPen(QColor(0, 240, 255, 110), 1.5));
        p.drawPath(path);
    }

    void enterEvent(QEnterEvent *) override { if (onMouseEnter) onMouseEnter(); }
    void leaveEvent(QEvent *) override { if (onMouseLeave) onMouseLeave(); }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) close();
        QWidget::keyPressEvent(e);
    }
};

struct Vec3 {
    double x, y, z;
};

struct ProjectedPoint {
    QPointF p;
    double z;
    double scale;
};

struct Spark {
    double radius;
    double angle;
    double speed;
    double size;
    QColor color;
};

class HoloCoreWidget : public QWidget {
    Q_OBJECT

    QTimer *timer;
    QTimer *dismissTimer;
    NeonCommandPanel *panel = nullptr;
    HistoryPanel *historyPanel = nullptr;
    SettingsPanel *settingsPanel = nullptr;

    double t = 0.0;
    double spin = 0.0;
    bool processing = false;
    bool unread = false;
    double processingPulse = 0.0;

    std::vector<Vec3> vertices;
    std::vector<std::array<int, 3>> faces;
    std::vector<std::pair<int, int>> edges;
    std::vector<Spark> sparks;

public:
    HoloCoreWidget() {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::X11BypassWindowManagerHint);
        setFixedSize(130, 130);
        setMouseTracking(true);

        buildGeometry();
        buildSparks();

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            t += 0.016;
            spin += processing ? 0.045 : 0.014;
            if (processing) processingPulse += 0.18;
            update();
        });
        timer->start(16);

        dismissTimer = new QTimer(this);
        dismissTimer->setSingleShot(true);
        dismissTimer->setInterval(1100);
        connect(dismissTimer, &QTimer::timeout, this, [this]() {
            if (panel && !panel->isRequestInFlight() && !panel->isPinned()) {
                panel->close();
                panel->deleteLater();
                panel = nullptr;
            }
        });
    }

    void setProcessing(bool on) {
        processing = on;
        if (!on) processingPulse = 0.0;
        update();
    }

    void applyConfiguredPosition() {
        if (auto *screen = QGuiApplication::primaryScreen()) {
            QRect sg = screen->geometry();
            QString pos = positionSetting();
            int ox = offsetXSetting();
            int oy = offsetYSetting();
            int x = sg.left();
            int y = sg.top();

            if (pos.contains("middle")) {
                x = sg.left() + (sg.width() - width()) / 2;
            } else if (pos.contains("right")) {
                x = sg.right() - width();
            } else {
                x = sg.left();
            }

            if (pos.startsWith("bottom")) {
                y = sg.bottom() - height();
            } else {
                y = sg.top();
            }

            move(x + ox, y + oy);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        QPointF c(width() / 2.0, height() / 2.0);
        double pulse = processing ? 0.5 + 0.5 * std::sin(processingPulse) : 0.0;
        double coreScale = processing ? 1.0 + pulse * 0.08 : 1.0;

        drawAmbientGlow(p, c, pulse);
        drawSparks(p, c, pulse);
        drawCrystal(p, c, coreScale, pulse);
        drawUnreadBadge(p, c);
    }

    void enterEvent(QEnterEvent *) override {
        dismissTimer->stop();
        if (panel) {
            panel->show();
            panel->raise();
            return;
        }
        openInput();
    }

    void leaveEvent(QEvent *) override {
        if (panel && !panel->isRequestInFlight() && !panel->isPinned()) {
            dismissTimer->start();
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            showLastResponse();
            return;
        }
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        showContextMenu(e->globalPos());
    }

private:
    QPoint panelAnchor(int dx = -6, int dy = 26) {
        return mapToGlobal(QPoint(width() - dx, dy));
    }

    QPoint smartPanelPos(QWidget *w, int preferredYOffset = 0) {
        QPoint pos = mapToGlobal(QPoint(width() + 6, preferredYOffset));

        if (auto *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()))) {
            QRect sg = screen->availableGeometry();
            QString configured = positionSetting();
            int margin = 12;

            // If the jewel is anchored near the bottom, open panels upward from it
            // instead of letting them hang into the damaged/taskbar area.
            if (configured.startsWith("bottom")) {
                pos.setY(mapToGlobal(QPoint(0, height())).y() - w->height() - 8);
            } else {
                pos.setY(mapToGlobal(QPoint(0, 0)).y() + height() + 8);
            }

            if (configured.contains("right")) {
                pos.setX(mapToGlobal(QPoint(0, 0)).x() - w->width() - 8);
            } else if (configured.contains("middle")) {
                pos.setX(mapToGlobal(QPoint(width() / 2, 0)).x() - w->width() / 2);
            } else {
                pos.setX(mapToGlobal(QPoint(width(), 0)).x() + 8);
            }

            if (pos.x() + w->width() > sg.right() - margin) pos.setX(sg.right() - margin - w->width());
            if (pos.x() < sg.left() + margin) pos.setX(sg.left() + margin);
            if (pos.y() + w->height() > sg.bottom() - margin) pos.setY(sg.bottom() - margin - w->height());
            if (pos.y() < sg.top() + margin) pos.setY(sg.top() + margin);
        }

        return pos;
    }

    void wirePanel(NeonCommandPanel *p) {
        p->onDismiss = [this]() { panel = nullptr; };
        p->onMouseEnter = [this]() { dismissTimer->stop(); };
        p->onMouseLeave = [this]() {
            if (panel && !panel->isRequestInFlight() && !panel->isPinned()) dismissTimer->start();
        };
        p->onHistoryAdded = [this](const HistoryEntry&) {
            unread = true;
            if (historyPanel) historyPanel->refresh();
            update();
        };
        p->onOpenHistory = [this]() { showHistory(); };
        p->onOpenSettings = [this]() { showSettings(); };
    }

    NeonCommandPanel* ensurePanel() {
        if (!panel) {
            panel = new NeonCommandPanel([this](bool isProcessing) { setProcessing(isProcessing); });
            wirePanel(panel);
            panel->move(panelAnchor());
        }
        return panel;
    }

    void openInput() {
        auto *p = ensurePanel();
        p->showInput();
        p->move(smartPanelPos(p));
    }

    void showLastResponse() {
        unread = false;
        update();
        auto *p = ensurePanel();
        p->showLastResponse();
        p->move(smartPanelPos(p));
    }

    void showHistory() {
        dismissTimer->stop();
        if (panel && !panel->isRequestInFlight() && !panel->isPinned()) panel->hide();
        if (settingsPanel) settingsPanel->hide();
        if (!historyPanel) {
            historyPanel = new HistoryPanel();
            historyPanel->onMouseEnter = [this]() { dismissTimer->stop(); };
            historyPanel->onMouseLeave = [this]() {
                QTimer::singleShot(900, this, [this]() {
                    if (historyPanel && !historyPanel->underMouse()) historyPanel->hide();
                });
            };
            historyPanel->onEntrySelected = [this](const HistoryEntry &e) {
                unread = false;
                update();
                auto *p = ensurePanel();
                p->showEntry(e);
                p->move(smartPanelPos(p));
                if (historyPanel) historyPanel->hide();
            };
        }
        historyPanel->refresh();
        historyPanel->show();
        historyPanel->move(smartPanelPos(historyPanel));
        historyPanel->raise();
        historyPanel->activateWindow();
    }

    void showSettings() {
        dismissTimer->stop();
        if (historyPanel) historyPanel->hide();
        if (!settingsPanel) {
            settingsPanel = new SettingsPanel();
            settingsPanel->onMouseEnter = [this]() { dismissTimer->stop(); };
            settingsPanel->onMouseLeave = [this]() {
                QTimer::singleShot(900, this, [this]() {
                    if (settingsPanel && !settingsPanel->underMouse()) settingsPanel->hide();
                });
            };
            settingsPanel->onSaved = [this]() {
                applyConfiguredPosition();
                if (panel) {
                    panel->showInput();
                    panel->move(smartPanelPos(panel));
                }
            };
        }
        settingsPanel->show();
        settingsPanel->move(smartPanelPos(settingsPanel));
        settingsPanel->raise();
        settingsPanel->activateWindow();
    }

    void showContextMenu(const QPoint &globalPos) {
        QMenu menu;
        menu.setStyleSheet(
            "QMenu { background: rgba(5,9,22,240); color: #E9FFFF; border: 1px solid rgba(64,240,255,140);"
            "border-radius: 10px; padding: 6px; }"
            "QMenu::item { padding: 7px 28px 7px 18px; border-radius: 7px; }"
            "QMenu::item:selected { background: rgba(255,166,48,48); }"
        );
        QAction *last = menu.addAction("Last Response");
        QAction *hist = menu.addAction("History");
        QAction *sett = menu.addAction("Settings");
        menu.addSeparator();
        QAction *agent = menu.addAction("Agent: " + agentNameSetting());
        agent->setEnabled(false);
        menu.addSeparator();
        QAction *quit = menu.addAction("Quit");

        QAction *chosen = menu.exec(globalPos);
        if (chosen == last) showLastResponse();
        else if (chosen == hist) showHistory();
        else if (chosen == sett) showSettings();
        else if (chosen == quit) qApp->quit();
    }

    void buildGeometry() {
        vertices = {
            {0, 1.28, 0}, {0, -1.28, 0},
            {1.05, 0, 0}, {-1.05, 0, 0},
            {0, 0, 1.05}, {0, 0, -1.05},
            {0.56, 0.56, 0.56}, {-0.56, 0.56, 0.56},
            {-0.56, 0.56, -0.56}, {0.56, 0.56, -0.56},
            {0.56, -0.56, 0.56}, {-0.56, -0.56, 0.56},
            {-0.56, -0.56, -0.56}, {0.56, -0.56, -0.56}
        };

        faces = {
            {0, 6, 7}, {0, 7, 8}, {0, 8, 9}, {0, 9, 6},
            {1, 11, 10}, {1, 12, 11}, {1, 13, 12}, {1, 10, 13},
            {2, 6, 10}, {2, 10, 13}, {2, 13, 9}, {2, 9, 6},
            {3, 7, 11}, {3, 12, 8}, {3, 11, 12}, {3, 8, 7},
            {4, 7, 6}, {4, 10, 11}, {4, 6, 10}, {4, 11, 7},
            {5, 9, 8}, {5, 12, 13}, {5, 8, 12}, {5, 13, 9}
        };

        std::vector<std::pair<int, int>> raw;
        for (auto f : faces) {
            raw.push_back(normEdge(f[0], f[1]));
            raw.push_back(normEdge(f[1], f[2]));
            raw.push_back(normEdge(f[2], f[0]));
        }
        std::sort(raw.begin(), raw.end());
        raw.erase(std::unique(raw.begin(), raw.end()), raw.end());
        edges = raw;
    }

    std::pair<int, int> normEdge(int a, int b) {
        return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    void buildSparks() {
        std::mt19937 rng(1337);
        std::uniform_real_distribution<double> r(26, 58);
        std::uniform_real_distribution<double> a(0, PI * 2);
        std::uniform_real_distribution<double> s(-0.8, 0.8);
        std::uniform_real_distribution<double> z(0.6, 1.6);
        for (int i = 0; i < 16; ++i) {
            QColor col = (i % 3 == 0) ? QColor(255, 166, 48) : ((i % 3 == 1) ? QColor(0, 240, 255) : QColor(140, 255, 80));
            sparks.push_back({r(rng), a(rng), s(rng), z(rng), col});
        }
    }

    Vec3 rotate(Vec3 v) const {
        double ay = spin;
        double ax = std::sin(t * 0.61) * 0.36 + 0.22;
        double az = std::cos(t * 0.43) * 0.18;

        double cy = std::cos(ay), sy = std::sin(ay);
        double cx = std::cos(ax), sx = std::sin(ax);
        double cz = std::cos(az), sz = std::sin(az);

        double x1 = v.x * cy + v.z * sy;
        double y1 = v.y;
        double z1 = -v.x * sy + v.z * cy;

        double x2 = x1;
        double y2 = y1 * cx - z1 * sx;
        double z2 = y1 * sx + z1 * cx;

        return {x2 * cz - y2 * sz, x2 * sz + y2 * cz, z2};
    }

    ProjectedPoint project(Vec3 v, QPointF c, double scale = 1.0) const {
        Vec3 r = rotate(v);
        double distance = 4.2;
        double perspective = distance / (distance - r.z);
        double s = 31.0 * perspective * scale;
        return {QPointF(c.x() + r.x * s, c.y() - r.y * s), r.z, perspective};
    }

    void drawAmbientGlow(QPainter &p, QPointF c, double pulse) {
        double boost = processing ? 1.0 + pulse * 0.9 : 1.0;
        QRadialGradient g(c, 60);
        g.setColorAt(0.0, QColor(0, 255, 255, int(10 * boost)));
        g.setColorAt(0.28, QColor(70, 30, 255, int(10 * boost)));
        g.setColorAt(0.55, QColor(255, 166, 48, int(7 * boost)));
        g.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(c, 60, 60);

        QRadialGradient hot(c, 26 + pulse * 8);
        hot.setColorAt(0.0, QColor(255, 255, 255, processing ? 90 : 45));
        hot.setColorAt(0.3, QColor(0, 240, 255, processing ? 70 : 36));
        hot.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setBrush(hot);
        p.drawEllipse(c, 35 + pulse * 10, 35 + pulse * 10);
    }

    void drawOrbitRings(QPainter &p, QPointF c, double pulse) {
        for (int i = 0; i < 3; ++i) {
            p.save();
            double phase = t * (28 + i * 13) + i * 120;
            p.translate(c);
            p.rotate(phase);
            p.scale(1.0, 0.27 + i * 0.08);
            QColor col = i == 0 ? QColor(0, 240, 255) : (i == 1 ? QColor(255, 166, 48) : QColor(160, 255, 80));
            col.setAlpha(processing ? 115 + int(pulse * 90) : 74);
            QPen pen(col, processing ? 1.8 + pulse * 1.2 : 1.15);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);

            QRectF ring(-88 - i * 11, -88 - i * 11, 176 + i * 22, 176 + i * 22);
            p.drawArc(ring, 20 * 16, 270 * 16);
            col.setAlpha(processing ? 190 : 130);
            p.setPen(QPen(col, 3.0));
            p.drawArc(ring, int((phase * 7)) * 16, 24 * 16);
            p.restore();
        }
    }

    void drawSparks(QPainter &p, QPointF c, double pulse) {
        p.setPen(Qt::NoPen);
        for (size_t i = 0; i < sparks.size(); ++i) {
            Spark &s = sparks[i];
            double a = s.angle + t * s.speed + std::sin(t * 0.9 + i) * 0.05;
            double wobble = std::sin(t * 1.7 + i * 0.31) * 7.0;
            QPointF pos(c.x() + std::cos(a) * (s.radius + wobble),
                        c.y() + std::sin(a) * (s.radius * 0.45 + wobble * 0.35));
            QColor col = s.color;
            col.setAlpha(processing ? 70 + int(pulse * 45) : 24);
            p.setBrush(col);
            p.drawEllipse(pos, s.size * 0.55, s.size * 0.55);
        }
    }

    void drawCrystal(QPainter &p, QPointF c, double coreScale, double pulse) {
        std::vector<ProjectedPoint> pp;
        pp.reserve(vertices.size());
        for (auto v : vertices) pp.push_back(project(v, c, coreScale));

        struct FaceDraw {
            double z;
            QPolygonF poly;
            QColor color;
        };
        std::vector<FaceDraw> drawFaces;

        for (size_t i = 0; i < faces.size(); ++i) {
            auto f = faces[i];
            double z = (pp[f[0]].z + pp[f[1]].z + pp[f[2]].z) / 3.0;
            double bright = std::clamp((z + 1.2) / 2.4, 0.0, 1.0);
            QColor col;
            if (i % 3 == 0) col = QColor(0, 230, 255, int(14 + bright * 38 + pulse * 18));
            else if (i % 3 == 1) col = QColor(255, 176, 64, int(10 + bright * 32 + pulse * 14));
            else col = QColor(140, 255, 80, int(8 + bright * 24 + pulse * 10));

            QPolygonF poly;
            poly << pp[f[0]].p << pp[f[1]].p << pp[f[2]].p;
            drawFaces.push_back({z, poly, col});
        }

        std::sort(drawFaces.begin(), drawFaces.end(), [](const FaceDraw &a, const FaceDraw &b) {
            return a.z < b.z;
        });

        p.setPen(Qt::NoPen);
        for (auto &fd : drawFaces) {
            p.setBrush(fd.color);
            p.drawPolygon(fd.poly);
        }

        for (int pass = 0; pass < 3; ++pass) {
            int alpha = pass == 0 ? 22 : (pass == 1 ? 82 : 210);
            double width = pass == 0 ? 3.2 + pulse * 2.0 : (pass == 1 ? 1.6 + pulse * 1.0 : 0.9);
            QColor edgeColor = pass == 2 ? QColor(230, 255, 255, alpha) : QColor(0, 240, 255, alpha);
            if (processing && pass < 2) edgeColor = QColor(255, 166, 48, alpha + int(pulse * 30));
            QPen pen(edgeColor, width);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            for (auto e : edges) {
                p.drawLine(pp[e.first].p, pp[e.second].p);
            }
        }

        QColor nodeColor(255, 255, 255, processing ? 230 : 180);
        p.setPen(Qt::NoPen);
        p.setBrush(nodeColor);
        for (auto &pt : pp) {
            double r = (1.0 + pt.scale * 0.9) * (processing ? 1.0 + pulse * 0.35 : 1.0);
            p.drawEllipse(pt.p, r, r);
        }
    }

    void drawHudTicks(QPainter &p, QPointF c, double pulse) {
        p.save();
        p.translate(c);
        p.rotate(-t * 22.0);
        for (int i = 0; i < 36; ++i) {
            double a = i * PI * 2.0 / 36.0;
            double r1 = (i % 6 == 0) ? 112 : 118;
            double r2 = 124;
            QColor col = (i % 6 == 0) ? QColor(0, 240, 255, processing ? 145 : 84)
                                      : QColor(255, 255, 255, processing ? 65 + int(pulse * 55) : 32);
            p.setPen(QPen(col, (i % 6 == 0) ? 1.4 : 0.8));
            p.drawLine(QPointF(std::cos(a) * r1, std::sin(a) * r1),
                       QPointF(std::cos(a) * r2, std::sin(a) * r2));
        }
        p.restore();

        p.setPen(QPen(QColor(0, 240, 255, processing ? 130 : 70), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 124, 124);

        if (processing) {
            QString label = "PROCESSING";
            p.setPen(QPen(QColor(220, 255, 255, 180), 1));
            QFont f = p.font();
            f.setPixelSize(11);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 2.2);
            p.setFont(f);
            QRectF rr(c.x() - 80, c.y() + 94, 160, 18);
            p.drawText(rr, Qt::AlignCenter, label);
        }
    }

    void drawUnreadBadge(QPainter &p, QPointF c) {
        if (!unread) return;
        double r = 4.5 + 1.2 * (0.5 + 0.5 * std::sin(t * 5.0));
        QPointF pos(c.x() + 39, c.y() - 38);
        QRadialGradient g(pos, 10);
        g.setColorAt(0.0, QColor(255, 255, 255, 230));
        g.setColorAt(0.25, QColor(255, 166, 48, 200));
        g.setColorAt(1.0, QColor(255, 166, 48, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(pos, 10, 10);
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawEllipse(pos, r * 0.45, r * 0.45);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    HoloCoreWidget w;
    w.show();

    w.applyConfiguredPosition();

    return app.exec();
}

#include "main_cool.moc"
