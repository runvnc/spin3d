#pragma once

#include "app_config.h"

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
