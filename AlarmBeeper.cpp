#include "AlarmBeeper.h"

void beep(int pin, bool activeHigh, unsigned long durationMs) {
	digitalWrite(pin, activeHigh ? HIGH : LOW);
	if (durationMs > 0) delay(durationMs);
	digitalWrite(pin, activeHigh ? LOW : HIGH);
}

void beepShort(int pin, bool activeHigh) {
	beep(pin, activeHigh, 150);
}

void beepLong(int pin, bool activeHigh) {
	beep(pin, activeHigh, 2000);
}

void beepOff(int pin, bool activeHigh) {
	digitalWrite(pin, activeHigh ? LOW : HIGH);
}

void beepPattern(int pin, bool activeHigh, uint8_t times, unsigned long onMs, unsigned long offMs) {
	for (uint8_t i = 0; i < times; ++i) {
		digitalWrite(pin, activeHigh ? HIGH : LOW);
		if (onMs > 0) delay(onMs);
		digitalWrite(pin, activeHigh ? LOW : HIGH);
		if (offMs > 0) delay(offMs);
	}
}
