#include "timer.h"
#include "io.h"

volatile uint64_t system_ticks = 0;

void timer_handler(void) {
    system_ticks++;
    outb(0x20, 0x20);
}
