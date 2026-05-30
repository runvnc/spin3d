#include <atomic>
#include <functional>
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QScreen>
#include <QWindow>
#include <cmath>
#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QProcessEnvironment>
#include <QKeyEvent>
#include <QTextBrowser>
#include <QPixmap>
#include <QEventLoop>
#include <cstdio>

class MarkdownBrowser : public QTextBrowser {
    Q_OBJECT
    QNetworkAccessManager *net;
public:
    MarkdownBrowser(QWidget *parent = nullptr) : QTextBrowser(parent) {
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

class FloatingTextInput : public QWidget {
    Q_OBJECT
    QLineEdit *lineEdit;
    MarkdownBrowser *resultBrowser;
    QNetworkAccessManager *net;
    std::atomic<bool> requestInFlight{false};
    std::function<void(bool)> processingCallback;
public:
    std::function<void()> onMouseEnter;
    std::function<void()> onMouseLeave;
    std::function<void()> onDismiss;

    FloatingTextInput(std::function<void(bool)> cb, QWidget *parent = nullptr) : QWidget(parent), processingCallback(cb) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(420, 60);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 8, 8, 8);
        lineEdit = new QLineEdit(this);
        lineEdit->setPlaceholderText("");
        lineEdit->setCursorPosition(0);
        lineEdit->setStyleSheet("QLineEdit {\n"
            "  background: rgba(0, 0, 0, 100);"
            "  color: #00FF00;"
            "  border: 1px solid rgba(0, 255, 0, 120);"
            "  border-radius: 8px;"
            "  padding: 10px 14px;"
            "  font-size: 18px;"
            "  font-family: 'Courier New', monospace;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid rgba(0, 255, 0, 220);"
            "  background: rgba(0, 0, 0, 140);"
            "}");
        setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(lineEdit);

        resultBrowser = new MarkdownBrowser(this);
        resultBrowser->setStyleSheet("MarkdownBrowser {\n"
            "  color: rgba(0, 255, 0, 200);"
            "  font-size: 16px;"
            "  font-family: 'Courier New', monospace;"
            "  padding: 6px 10px;"
            "  background: rgba(0, 0, 0, 200);"
            "  border-radius: 4px;"
            "  border: none;"
            "}");
        resultBrowser->setMaximumHeight(400);
        resultBrowser->setMinimumWidth(380);
        resultBrowser->setOpenExternalLinks(true);
        resultBrowser->hide();
        layout->addWidget(resultBrowser);

        auto *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(30);
        shadow->setColor(QColor(0, 255, 0, 120));
        shadow->setOffset(0, 0);
        lineEdit->setGraphicsEffect(shadow);

        net = new QNetworkAccessManager(this);
        connect(lineEdit, &QLineEdit::returnPressed, this, [this]() {
            QString text = lineEdit->text();
            if (text.isEmpty()) return;
            requestInFlight = true;
            resultBrowser->hide();
            setFixedSize(420, 60);
            fprintf(stderr, "[spin3d] Sending task: %s\n", text.toUtf8().constData());
            if (processingCallback) processingCallback(true);
            lineEdit->clear();
            this->hide();
            lineEdit->setEnabled(false);
            auto env = QProcessEnvironment::systemEnvironment();
            QString port = env.value("MR_PORT", "8022");
            QUrl url(QString("http://localhost:%1/task/DiscountAgent").arg(port));
            fprintf(stderr, "[spin3d] URL: %s\n", url.toString().toUtf8().constData());
            QString apiKey = env.value("MR_KEY", "");
            fprintf(stderr, "[spin3d] API key found: %s\n", apiKey.isEmpty() ? "NO" : "YES");
            if (!apiKey.isEmpty()) {
                QUrlQuery query;
                query.addQueryItem("api_key", apiKey);
                url.setQuery(query);
            }
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            QJsonObject body;
            body["instructions"] = text;
            QNetworkReply *reply = net->post(req, QJsonDocument(body).toJson());
            fprintf(stderr, "[spin3d] POST body: %s\n", QJsonDocument(body).toJson(QJsonDocument::Compact).constData());

            QTimer *timeout = new QTimer(this);
            timeout->setSingleShot(true);
            timeout->setInterval(120000);
            connect(timeout, &QTimer::timeout, this, [this, reply, timeout]() {
                fprintf(stderr, "[spin3d] TIMEOUT\n");
                if (processingCallback) processingCallback(false);
                lineEdit->setEnabled(true);
                resultBrowser->setPlainText("Timeout waiting for response.");
                setFixedSize(420, 120);
                lineEdit->hide();
                this->show();
                this->raise();
                this->activateWindow();
                resultBrowser->show();
                reply->abort();
                reply->deleteLater();
                requestInFlight = false;
                timeout->deleteLater();
            });
            timeout->start();

            connect(reply, &QNetworkReply::finished, this, [this, reply, timeout]() {
                timeout->stop();
                timeout->deleteLater();
                QByteArray respData = reply->readAll();
                requestInFlight = false;
                fprintf(stderr, "[spin3d] Response: %s\n", respData.constData());
                fprintf(stderr, "[spin3d] HTTP status: %d\n", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
                fprintf(stderr, "[spin3d] Calling processingCallback(false)\n");
                if (processingCallback) processingCallback(false);
                lineEdit->clear();
                lineEdit->setEnabled(true);

                if (reply->error() != QNetworkReply::NoError) {
                    resultBrowser->setPlainText("Error: " + reply->errorString());
                    setFixedSize(420, 120);
                    lineEdit->hide();
                    this->show();
                    this->raise();
                    this->activateWindow();
                    resultBrowser->show();
                    resultBrowser->setVisible(true);
                    this->update();
                    this->repaint();
                    reply->deleteLater();
                    return;
                }

                QJsonDocument doc = QJsonDocument::fromJson(respData);
                QJsonObject obj = doc.object();
                if (obj["status"].toString() == "ok") {
                    QString results = obj["result"].toString();
                    if (results.isEmpty()) {
                        results = obj["results"].toString();
                    }
                    if (results.isEmpty()) {
                        results = obj["output"].toString();
                    }
                    if (results.isEmpty()) {
                        results = "Task completed but returned no output.";
                    }
                    fprintf(stderr, "[spin3d] Setting result text: %s\n", results.toUtf8().constData());
                    resultBrowser->setMarkdown(results);
                    resultBrowser->document()->adjustSize();
                    int dh = qMin(400, (int)resultBrowser->document()->size().height() + 20);
                    setFixedSize(420, 80 + dh);
                    resultBrowser->setFixedWidth(380);
                    lineEdit->hide();
                    this->show();
                    this->raise();
                    this->activateWindow();
                    resultBrowser->show();
                    resultBrowser->setVisible(true);
                    this->update();
                    this->repaint();
                } else {
                    resultBrowser->setPlainText("Unexpected response: " + QString::fromUtf8(respData.left(200)));
                    setFixedSize(420, 120);
                    lineEdit->hide();
                    this->show();
                    this->raise();
                    this->activateWindow();
                    resultBrowser->show();
                    resultBrowser->setVisible(true);
                    this->update();
                    this->repaint();
                }
                reply->deleteLater();
            });
        });
    }

    bool isRequestInFlight() const { return requestInFlight; }
    bool isShowingResult() const { return resultBrowser && resultBrowser->isVisible(); }

    void showInput() {
        lineEdit->show();
        resultBrowser->hide();
        setFixedSize(420, 60);
        this->show();
        this->raise();
        this->activateWindow();
        lineEdit->setFocus();
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            if (onDismiss) onDismiss();
            close();
            deleteLater();
        }
        QWidget::keyPressEvent(e);
    }

protected:
    void enterEvent(QEnterEvent *) override {
        if (onMouseEnter) onMouseEnter();
    }
    void leaveEvent(QEvent *) override {
        if (onMouseLeave) onMouseLeave();
    }
};

class DiamondWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
    float angleY = 0.0f;
    float pulsePhase = 0.0f;
    QTimer *timer;
    QTimer *pulseTimer = nullptr;
    FloatingTextInput *textInput = nullptr;
    QTimer *dismissTimer;

public:
    DiamondWidget() {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::X11BypassWindowManagerHint);
        setFixedSize(200, 200);
        setMouseTracking(true);
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, [this]() {
            float speed = (pulseTimer && pulseTimer->isActive()) ? 12.0f : 2.4f;
            angleY += speed;
            update();
        });
        timer->start(16);

        dismissTimer = new QTimer(this);
        dismissTimer->setSingleShot(true);
        dismissTimer->setInterval(800);
        connect(dismissTimer, &QTimer::timeout, [this]() {
            if (textInput && !textInput->isRequestInFlight()) {
                textInput->close();
                textInput->deleteLater();
                textInput = nullptr;
            }
        });
    }

    void startPulse() {
        fprintf(stderr, "[spin3d] startPulse called\n");
        angleY = fmod(angleY, 360.0f);
        pulsePhase = 0.0f;
        if (!pulseTimer) {
            pulseTimer = new QTimer(this);
            connect(pulseTimer, &QTimer::timeout, [this]() { pulsePhase += 0.15f; update(); });
        }
        pulseTimer->start(30);
    }

    void stopPulse() {
        fprintf(stderr, "[spin3d] stopPulse called, isActive before: %d\n", pulseTimer ? pulseTimer->isActive() : 0);
        if (pulseTimer) pulseTimer->stop();
        fprintf(stderr, "[spin3d] stopPulse called, isActive after: %d\n", pulseTimer ? pulseTimer->isActive() : 0);
        pulsePhase = 0.0f;
        update();
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(0,0,0,0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void resizeGL(int w, int h) {
        glViewport(0,0,w,h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float a=(float)w/h;
        glOrtho(-2*a, 2*a, -2, 2, -10, 10);
        glMatrixMode(GL_MODELVIEW);
    }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0,0,-4);
        glRotatef(angleY, 0, 1, 0);
        float v[6][3]={{0,1.2f,0},{0,-1.2f,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        int f[8][3]={{0,2,4},{0,4,3},{0,3,5},{0,5,2},{1,4,2},{1,3,4},{1,5,3},{1,2,5}};
        int e[12][2]={{0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{4,3},{3,5},{5,2}};
        float pulse = pulseTimer && pulseTimer->isActive() ? 0.5f + 0.5f * sin(pulsePhase) : 0.0f;
        glLineWidth(pulse > 0.0f ? 1.2f + pulse * 2.2f : 2.0f);
        if (pulse > 0.0f) {
            glColor4f(0.0f, 0.5f + 0.5f * (1.0f - pulse), 1.0f, 0.6f + 0.4f * (1.0f - pulse));
        } else {
            glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
        }
        glBegin(GL_LINES);
        for(int i=0;i<12;i++){glVertex3fv(v[e[i][0]]);glVertex3fv(v[e[i][1]]);}
        glEnd();
    }

    void enterEvent(QEnterEvent *) override {
        dismissTimer->stop();
        if (textInput) {
            if (textInput->isShowingResult()) {
                textInput->showInput();
            }
            textInput->show();
            textInput->raise();
            textInput->activateWindow();
            return;
        }
        textInput = new FloatingTextInput([this](bool isProcessing) { if (isProcessing) startPulse(); else stopPulse(); });
        textInput->onDismiss = [this]() {
            textInput = nullptr;
        };
        textInput->onMouseEnter = [this]() { dismissTimer->stop(); };
        textInput->onMouseLeave = [this]() { if (!textInput->isRequestInFlight()) dismissTimer->start(); };
        int x = mapToGlobal(QPoint(200, 0)).x() + 5;
        int y = mapToGlobal(QPoint(0, 0)).y() + 5;
        textInput->move(x, y);
        textInput->showInput();
    }

    void leaveEvent(QEvent *) override {
        if (textInput && !textInput->isRequestInFlight()) {
            dismissTimer->start();
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (textInput && textInput->isShowingResult()) {
            textInput->showInput();
        }
    }
};
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    DiamondWidget w;
    w.show();
    if (auto *s = QGuiApplication::primaryScreen()) {
        QRect sg = s->geometry();
        w.move(-20, sg.bottom() - 50 - w.height());
    }
    return app.exec();
}
#include "main.moc"
