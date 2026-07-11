#ifndef TIMER_H
#define TIMER_H
#include "types.h"

extern volatile uint64_t system_ticks;
void timer_handler(void);

#endif
