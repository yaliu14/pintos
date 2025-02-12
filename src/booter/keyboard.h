#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "buffer.h"
#include "ports.h"
#include "interrupts.h"
#include "state.h"

void init_keyboard();
void keyboard_interrupt(void);

#endif /* KEYBOARD_H */
