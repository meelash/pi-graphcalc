// Double-precision maths for a freestanding kernel (there is no libm on the
// Pi). Accurate to a few units in the last place over the ranges a high
// school calculator uses; tests/math compares it against the host's libm.
#pragma once

namespace m {

const double kPi = 3.141592653589793238462643383279502884;
const double kE = 2.718281828459045235360287471352662498;
const double kLn2 = 0.693147180559945309417232121458176568;
const double kLn10 = 2.302585092994045684017991454684364208;

double Nan();
double Inf();
inline bool IsNan(double x) { return x != x; }
inline bool IsInf(double x) { return x == Inf() || x == -Inf(); }
inline bool IsFinite(double x) { return !IsNan(x) && !IsInf(x); }

inline double Abs(double x) { return x < 0 ? -x : (x == 0 ? 0.0 : x); }
double Floor(double x);
double Ceil(double x);
double Round(double x);  // halves away from zero
double Trunc(double x);
double Fmod(double x, double y);  // sign of x, like C fmod
double Sqrt(double x);
double Cbrt(double x);

double Exp(double x);
double Ln(double x);
double Log10(double x);
double Log2(double x);
double Pow(double x, double y);  // real powers; odd roots of negatives work: (-8)^(1/3) = -2
double Ldexp(double x, int e);   // x * 2^e

// Trigonometry in radians. Arguments within a rounding error of a multiple
// of pi/2 (e.g. the double nearest to pi) give exact zeros, so sin(pi) = 0.
double Sin(double x);
double Cos(double x);
double Tan(double x);
double Asin(double x);
double Acos(double x);
double Atan(double x);
double Atan2(double y, double x);
double Sinh(double x);
double Cosh(double x);
double Tanh(double x);

// The same in degrees; multiples of 90 degrees are exact.
double SinDeg(double x);
double CosDeg(double x);
double TanDeg(double x);

double Gamma(double x);      // Gamma function (factorial: Gamma(n + 1))
double Factorial(double n);  // n! for whole n (exact up to 22!), Gamma(n + 1) otherwise

} // namespace m
