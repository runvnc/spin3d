#pragma once

#include "ui_style.h"
#include "app_config.h"

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
