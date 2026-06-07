#include "src/holo_core_widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    HoloCoreWidget w;
    w.show();

    w.applyConfiguredPosition();

    return app.exec();
}
