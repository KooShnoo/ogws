#include "revolution/OS/OSReset.h"

extern void slugBug();

int main() {
    slugBug();
    OSShutdownSystem();
    return 0;
}