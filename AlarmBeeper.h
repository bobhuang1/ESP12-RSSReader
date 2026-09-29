#pragma once
#include <Arduino.h>

// All functions take activeHigh so the same code drives a buzzer/LED wired either way:
// activeHigh=true means HIGH energizes it, activeHigh=false means LOW energizes it.

// Pulses pin for durationMs, then returns it to the inactive level. Blocking (uses delay()).
void beep(int pin, bool activeHigh, unsigned long durationMs);

void beepShort(int pin, bool activeHigh); // 150ms - "acknowledged" / correct-answer beep
void beepLong(int pin, bool activeHigh);  // 2000ms - alert/warning beep
void beepOff(int pin, bool activeHigh);   // sets pin to its inactive level, no delay

// A repeated short pulse pattern, e.g. for a "wrong answer" buzz or a rapid Geiger click.
// times=8, onMs=30, offMs=30 gives a "wrong answer" buzz; times=1, onMs=1, offMs=0
// gives a single rapid click (e.g. a Geiger-counter pulse).
void beepPattern(int pin, bool activeHigh, uint8_t times, unsigned long onMs, unsigned long offMs);
