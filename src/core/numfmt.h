// Numbers to text and back, without a C library.
#pragma once
#include "base.h"

namespace num {

// 10^e as a double (exact for |e| <= 22).
double Pow10(int e);

// For people: up to sig significant digits, trailing zeros dropped,
// scientific as "1.5×10⁻⁷" outside 0.0001 .. 9999999999.
// NaN is "undefined", infinities are "∞" and "-∞".
void Format(double v, char *out, int cap, int sig = 10);

// For files and the edit line: ASCII only ("1.5E-7"), parses back with Parse().
void FormatPlain(double v, char *out, int cap, int sig = 15);

// Parses a number at s ("12", ".5", "3.25E-4"); returns the characters used
// (0 if there is no number there). The exponent marker is a capital E.
int Parse(const char *s, double *out);

} // namespace num
