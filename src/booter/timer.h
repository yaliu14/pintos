#ifndef TIMER_H
#define TIMER_H

#include "ports.h"
#include "game.h"

void init_timer(void);
void timer_interrupt(void);

void reset_t();

#endif /* TIMER_H */

