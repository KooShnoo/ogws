#include "revolution/OS/OSReset.h"

extern void slugBug();

int main() {
    slugBug();
    OSReturnToMenu();
    return 0;
}