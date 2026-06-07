#pragma once

#include "command_panel.h"
#include "settings_panel.h"
#include "history_panel.h"

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
    QString taskStatusText;

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

    void setTaskStatus(const QString &text) {
        taskStatusText = showTaskStatusSetting() ? text : QString();
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
        drawTaskStatus(p, c);
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
            panel->onTaskStatus = [this](const QString &status) { setTaskStatus(status); };
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

    void drawTaskStatus(QPainter &p, QPointF c) {
        if (!showTaskStatusSetting() || taskStatusText.isEmpty()) return;
        QFont f = p.font();
        f.setPixelSize(10);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        QFontMetrics fm(f);
        QString shown = fm.elidedText(taskStatusText, Qt::ElideRight, 104);
        QRectF box(c.x() - 54, height() - 24, 108, 16);
        QPainterPath path;
        path.addRoundedRect(box, 7, 7);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(5, 9, 22, 178));
        p.drawPath(path);
        p.setPen(QPen(QColor(0, 240, 255, 120), 1.0));
        p.drawPath(path);
        p.setPen(QColor(220, 255, 255, 210));
        p.drawText(box.adjusted(5, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft, shown);
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
