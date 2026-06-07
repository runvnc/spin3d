#pragma once

#include "spin3d_common.h"

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
