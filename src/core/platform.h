// What the calculator needs from the hardware. Implemented by the Pi kernel
// (src/pi) and by the desktop simulator (src/sim).
#pragma once
#include "base.h"

class Platform
{
public:
	virtual ~Platform() {}
	virtual u32 Millis() = 0;
	// Files on the SD card, e.g. "graphcalc/state.txt" (folders are created).
	virtual bool LoadFile(const char *name, char *buf, int cap, int *len) = 0;
	virtual bool SaveFile(const char *name, const char *buf, int len) = 0;
	virtual bool KeyboardPresent() = 0;
	virtual bool StorageAvailable() = 0;
	// Status and log lines (oldest first).
	virtual int Diagnostics(const char **lines, int max) { return 0; }
	// True when the card has the boot menu (pi-launcher) to go back to.
	virtual bool HasMenu() { return false; }
	// Restarts into the boot menu (on the Pi the main loop then ends).
	virtual void ReturnToMenu() {}
};
