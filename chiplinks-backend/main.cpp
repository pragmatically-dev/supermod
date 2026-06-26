#include "ChiplinksBackend.hpp"
#include <QtQml/qqml.h>

extern "C" void registerQmldiff() {
    qmlRegisterSingletonInstance<ChiplinksBackend>(
        "dev.pragmatically.chiplinks", 0, 1,
        "ChiplinksBackend", new ChiplinksBackend());
}
