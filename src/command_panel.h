#pragma once

#include "markdown_browser.h"
#include "ui_style.h"

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
    QNetworkReply *eventsReply = nullptr;
    QString sseBuffer;
    QString sseEvent;
    QString sseData;
    QString currentStatusText;
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
    std::function<void(const QString&)> onTaskStatus;

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

    QString paramValuesPreview(const QJsonObject &obj) {
        QJsonObject params = obj.contains("args") ? obj["args"].toObject() : obj["params"].toObject();
        if (params.isEmpty()) return QString();
        QString preview;
        for (auto it = params.begin(); it != params.end(); ++it) {
            QString val;
            if (it.value().isString()) val = it.value().toString();
            else if (it.value().isBool()) val = it.value().toBool() ? "true" : "false";
            else if (it.value().isDouble()) val = QString::number(it.value().toDouble());
            else val = QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact).left(60));
            if (!preview.isEmpty()) preview += " ";
            preview += val;
            if (preview.length() > 40) { preview = preview.left(40) + "…"; break; }
        }
        return preview;
    }

    void setTaskStatus(const QString &text) {
        currentStatusText = text;
        if (showTaskStatusSetting() && onTaskStatus) onTaskStatus(text);
    }

    void stopStatusStream() {
        if (eventsReply) {
            eventsReply->disconnect(this);
            eventsReply->abort();
            eventsReply->deleteLater();
            eventsReply = nullptr;
        }
        sseBuffer.clear();
        sseEvent.clear();
        sseData.clear();
    }

    void dispatchSseEvent(const QString &event, const QString &data) {
        if (!showTaskStatusSetting() || event.isEmpty()) return;
        QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        QJsonObject obj = doc.object();
        if (event == "running_command") {
            QString cmd = obj["command"].toString();
            QString pv = paramValuesPreview(obj);
            if (!cmd.isEmpty()) setTaskStatus("> " + cmd + (pv.isEmpty() ? "" : ": " + pv));
        } else if (event == "partial_command") {
            QString cmd = obj["command"].toString();
            QString pv = paramValuesPreview(obj);
            if (!cmd.isEmpty() && currentStatusText.isEmpty()) setTaskStatus("> " + cmd + (pv.isEmpty() ? "" : ": " + pv));
        } else if (event == "finished_chat") {
            setTaskStatus("");
        } else if (event == "system_error") {
            setTaskStatus("> error");
        }
    }

    void parseStatusStream() {
        if (!eventsReply) return;
        sseBuffer += QString::fromUtf8(eventsReply->readAll());
        int nl = -1;
        while ((nl = sseBuffer.indexOf('\n')) >= 0) {
            QString line = sseBuffer.left(nl);
            sseBuffer.remove(0, nl + 1);
            if (line.endsWith('\r')) line.chop(1);
            if (line.isEmpty()) {
                dispatchSseEvent(sseEvent, sseData.trimmed());
                sseEvent.clear();
                sseData.clear();
            } else if (line.startsWith("event:")) {
                sseEvent = line.mid(6).trimmed();
            } else if (line.startsWith("data:")) {
                if (!sseData.isEmpty()) sseData += "\n";
                sseData += line.mid(5).trimmed();
            }
        }
    }

    void startStatusStream(const QString &logId, const QString &port, const QString &apiKey) {
        stopStatusStream();
        if (!showTaskStatusSetting() || logId.isEmpty()) return;
        QUrl eventsUrl(QString("http://localhost:%1/chat/%2/events").arg(port, logId));
        QNetworkRequest eventsReq(eventsUrl);
        if (!apiKey.isEmpty()) eventsReq.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        eventsReply = net->get(eventsReq);
        connect(eventsReply, &QNetworkReply::readyRead, this, [this]() { parseStatusStream(); });
        connect(eventsReply, &QNetworkReply::finished, this, [this]() {
            if (eventsReply) {
                eventsReply->deleteLater();
                eventsReply = nullptr;
            }
        });
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
        if (!forceNew && followContext) contextToUse = followContext;
        else if (forceContinue) contextToUse = latestHistoryEntry();

        QString instructions = visibleText;

        requestInFlight = true;
        resultBrowser->hide();
        footer->hide();
        setFixedSize(560, followContext ? 112 : 92);
        fprintf(stderr, "[spin3d_cool] Sending task to %s: %s\n", agent.toUtf8().constData(), visibleText.toUtf8().constData());
        if (processingCallback) processingCallback(true);
        setTaskStatus("> task");

        lineEdit->clear();
        hide();
        lineEdit->setEnabled(false);

        auto env = QProcessEnvironment::systemEnvironment();
        QString port = env.value("MR_PORT", "8022");
        QString apiKey = env.value("MR_KEY", "");

        HistoryEntry pending;
        pending.id = shortId();
        pending.threadId = contextToUse ? (contextToUse->threadId.isEmpty() ? contextToUse->id : contextToUse->threadId) : ("thread_" + pending.id);
        pending.continuedFrom = contextToUse ? contextToUse->id : "";
        pending.createdAt = nowIso();
        pending.agentName = agent;
        pending.query = visibleText;

        auto failBeforePost = [this, pending](const QString &message) mutable {
            if (processingCallback) processingCallback(false);
            setTaskStatus("");
            lineEdit->setEnabled(true);
            requestInFlight = false;
            pending.status = "error";
            pending.response = message;
            appendHistory(pending);
            if (onHistoryAdded) onHistoryAdded(pending);
            showEntry(pending);
        };

        auto postTask = [this, agent, instructions, pending, port, apiKey](const QString &taskLogId) mutable {
            startStatusStream(taskLogId, port, apiKey);
            QUrl url(QString("http://localhost:%1/task/%2").arg(port, agent));
            if (!apiKey.isEmpty()) {
                QUrlQuery query;
                query.addQueryItem("api_key", apiKey);
                url.setQuery(query);
            }
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            QJsonObject body;
            body["instructions"] = instructions;
            if (!taskLogId.isEmpty()) body["log_id"] = taskLogId;
            QNetworkReply *reply = net->post(req, QJsonDocument(body).toJson());

            pending.logId = taskLogId;
            QString cancelLogId = taskLogId;
            QTimer *timeout = new QTimer(this);
            timeout->setSingleShot(true);
            timeout->setInterval(timeoutSetting());
            connect(timeout, &QTimer::timeout, this, [this, reply, timeout, pending, cancelLogId, port, apiKey]() mutable {
                fprintf(stderr, "[spin3d_cool] TIMEOUT\n");
                stopStatusStream();
                setTaskStatus("");
                if (processingCallback) processingCallback(false);
                lineEdit->setEnabled(true);
                requestInFlight = false;
                pending.status = "timeout";
                pending.response = "Timeout waiting for response.";
                appendHistory(pending);
                if (onHistoryAdded) onHistoryAdded(pending);
                showEntry(pending);
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
                stopStatusStream();
                setTaskStatus("");
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
                QString returnedLogId = obj["log_id"].toString();
                if (!returnedLogId.isEmpty()) pending.logId = returnedLogId;
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
        };

        QString existingLogId = (contextToUse && !contextToUse->logId.isEmpty()) ? contextToUse->logId : QString();
        if (!existingLogId.isEmpty()) {
            postTask(existingLogId);
            return;
        }

        QUrl makeUrl(QString("http://localhost:%1/makesession/%2").arg(port, agent));
        if (!apiKey.isEmpty()) {
            QUrlQuery q;
            q.addQueryItem("api_key", apiKey);
            makeUrl.setQuery(q);
        }
        QNetworkRequest makeReq(makeUrl);
        if (!apiKey.isEmpty()) makeReq.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        QNetworkReply *makeReply = net->get(makeReq);
        connect(makeReply, &QNetworkReply::finished, this, [this, makeReply, postTask, failBeforePost]() mutable {
            QByteArray data = makeReply->readAll();
            if (makeReply->error() != QNetworkReply::NoError) {
                failBeforePost("Error creating task session: " + makeReply->errorString());
                makeReply->deleteLater();
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QString logId = doc.object()["log_id"].toString();
            if (logId.isEmpty()) {
                failBeforePost("Error creating task session: missing log_id in response.");
                makeReply->deleteLater();
                return;
            }
            makeReply->deleteLater();
            postTask(logId);
        });
    }
};
