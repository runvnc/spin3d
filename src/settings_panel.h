#pragma once

#include "ui_style.h"
#include "app_config.h"

class SettingsPanel : public QWidget {
    Q_OBJECT
    QLineEdit *agentEdit;
    QComboBox *positionCombo;
    QSpinBox *offsetXSpin;
    QSpinBox *offsetYSpin;
    QSpinBox *timeoutSpin;
    QCheckBox *statusCheck;

public:
    std::function<void()> onMouseEnter;
    std::function<void()> onMouseLeave;
    std::function<void()> onSaved;

    SettingsPanel(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(450, 470);

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


        statusCheck = new QCheckBox("Show live task command status near spin icon", this);
        statusCheck->setChecked(showTaskStatusSetting());
        statusCheck->setStyleSheet("QCheckBox { color: rgba(210,255,255,170); font-size: 13px; } QCheckBox::indicator { width: 16px; height: 16px; }");
        layout->addWidget(statusCheck);

        auto *statusHint = new QLabel("Shows tiny updates like > write or > think while a task is running.", this);
        statusHint->setWordWrap(true);
        statusHint->setStyleSheet("QLabel { color: rgba(190,245,255,105); font-size: 12px; }");
        layout->addWidget(statusHint);

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
            setShowTaskStatusSetting(statusCheck->isChecked());
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
