#pragma once

#include "spin3d_common.h"

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

static bool showTaskStatusSetting() {
    QSettings s("MindRoot", "spin3d_cool");
    return s.value("showTaskStatus", true).toBool();
}

static void setShowTaskStatusSetting(bool on) {
    QSettings s("MindRoot", "spin3d_cool");
    s.setValue("showTaskStatus", on);
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
