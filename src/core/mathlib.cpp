#include "mathlib.h"

namespace m {

namespace {

union Bits
{
	double d;
	unsigned long long u;
};

inline unsigned long long ToBits(double x)
{
	Bits b;
	b.d = x;
	return b.u;
}

inline double FromBits(unsigned long long u)
{
	Bits b;
	b.u = u;
	return b.d;
}

const double kTwo52 = 4503599627370496.0;  // 2^52: doubles at or above are whole

// Splits of ln 2 and pi/2 into parts whose products with small integers are
// exact (Cody-Waite reduction, constants from fdlibm).
const double kLn2Hi = 6.93147180369123816490e-01;
const double kLn2Lo = 1.90821492927058770002e-10;
const double kPio2_1 = 1.57079632673412561417e+00;
const double kPio2_2 = 6.07710050630396597660e-11;
const double kPio2_3 = 2.02226624871116645580e-21;
const double kPio2_3t = 8.47842766036889956997e-32;
const double kSqrt2 = 1.41421356237309504880;
const double kSqrt3 = 1.73205080756887729353;

// Exact powers of ten (all representable).
const double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
			 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

// sin(r) and cos(r) for |r| <= pi/4 (Taylor series, plenty of terms).
double SinPoly(double r)
{
	double z = r * r;
	double p = 1.0 / 51090942171709440000.0;  // 1/21!
	p = p * z - 1.0 / 121645100408832000.0;    // 1/19!
	p = p * z + 1.0 / 355687428096000.0;       // 1/17!
	p = p * z - 1.0 / 1307674368000.0;         // 1/15!
	p = p * z + 1.0 / 6227020800.0;            // 1/13!
	p = p * z - 1.0 / 39916800.0;              // 1/11!
	p = p * z + 1.0 / 362880.0;                // 1/9!
	p = p * z - 1.0 / 5040.0;                  // 1/7!
	p = p * z + 1.0 / 120.0;                   // 1/5!
	p = p * z - 1.0 / 6.0;                     // 1/3!
	return r + r * z * p;
}

double CosPoly(double r)
{
	double z = r * r;
	double p = -1.0 / 1124000727777607680000.0;  // -1/22!
	p = p * z + 1.0 / 2432902008176640000.0;      // 1/20!
	p = p * z - 1.0 / 6402373705728000.0;         // 1/18!
	p = p * z + 1.0 / 20922789888000.0;           // 1/16!
	p = p * z - 1.0 / 87178291200.0;              // 1/14!
	p = p * z + 1.0 / 479001600.0;                // 1/12!
	p = p * z - 1.0 / 3628800.0;                  // 1/10!
	p = p * z + 1.0 / 40320.0;                    // 1/8!
	p = p * z - 1.0 / 720.0;                      // 1/6!
	p = p * z + 1.0 / 24.0;                       // 1/4!
	double hz = 0.5 * z;
	double w = 1.0 - hz;
	// Keeps the precision of 1 - z/2 (fdlibm's trick).
	return w + (((1.0 - w) - hz) + z * z * p);
}

// Reduces x to r in [-pi/4, pi/4] with x = r + q * pi/2; returns q mod 4.
// r is set to exactly 0 when x is within its own rounding error of a
// multiple of pi/2 (so sin(pi) = 0, not 1.2e-16).
int ReducePio2(double x, double *r)
{
	double k = Round(x * (2.0 / kPi));
	double y = x - k * kPio2_1;
	y -= k * kPio2_2;
	y -= k * kPio2_3;
	y -= k * kPio2_3t;
	if (k != 0 && Abs(y) <= Abs(x) * 2.3e-16)
		y = 0;
	*r = y;
	long long q = (long long)Fmod(k, 4.0);
	return (int)((q + 4) % 4);
}

double SinQ(int q, double r)
{
	switch (q) {
	case 0: return SinPoly(r);
	case 1: return CosPoly(r);
	case 2: return -SinPoly(r);
	default: return -CosPoly(r);
	}
}

double CosQ(int q, double r)
{
	switch (q) {
	case 0: return CosPoly(r);
	case 1: return -SinPoly(r);
	case 2: return -CosPoly(r);
	default: return SinPoly(r);
	}
}

double TanQ(int q, double r)
{
	double s = SinQ(q, r), c = CosQ(q, r);
	if (c == 0)
		return Nan();  // tan(90 degrees) is undefined
	return s / c;
}

// atan for 0 <= t <= 1.
double AtanUnit(double t)
{
	double base = 0;
	// tan(15 degrees): above it, use atan(t) = pi/6 + atan((t*sqrt3 - 1) / (sqrt3 + t)).
	if (t > 0.26794919243112270) {
		t = (t * kSqrt3 - 1.0) / (kSqrt3 + t);
		base = kPi / 6;
	}
	double z = t * t;
	double p = 0;
	for (int n = 29; n >= 3; n -= 2)
		p = p * z + ((n / 2) % 2 ? -1.0 : 1.0) / n;
	return base + (t + t * z * p);
}

} // namespace

double Nan() { return FromBits(0x7FF8000000000000ull); }
double Inf() { return FromBits(0x7FF0000000000000ull); }

double Trunc(double x)
{
	if (IsNan(x) || Abs(x) >= kTwo52)
		return x;
	return (double)(long long)x;
}

double Floor(double x)
{
	double t = Trunc(x);
	return t > x ? t - 1 : t;
}

double Ceil(double x)
{
	double t = Trunc(x);
	return t < x ? t + 1 : t;
}

double Round(double x)
{
	double t = Trunc(x);
	double d = x - t;  // exact
	if (d >= 0.5)
		return t + 1;
	if (d <= -0.5)
		return t - 1;
	return t;
}

double Fmod(double x, double y)
{
	if (y == 0 || !IsFinite(x) || IsNan(y))
		return Nan();
	if (IsInf(y))
		return x;
	double q = Trunc(x / y);
	double r = __builtin_fma(-q, y, x);  // exact for a whole q (one rounding: none needed)
	// Correct a quotient that rounded the wrong way.
	if (x >= 0 ? r < 0 : r > 0)
		r += x >= 0 ? Abs(y) : -Abs(y);
	if (Abs(r) >= Abs(y))
		r -= x >= 0 ? Abs(y) : -Abs(y);
	return r;
}

double Sqrt(double x)
{
	if (x < 0)
		return Nan();
#if defined(__aarch64__)
	double r;
	asm("fsqrt %d0, %d1" : "=w"(r) : "w"(x));
	return r;
#else
	return __builtin_sqrt(x);
#endif
}

double Ldexp(double x, int e)
{
	while (e > 1000) {
		x *= FromBits((unsigned long long)(1023 + 1000) << 52);
		e -= 1000;
	}
	while (e < -1000) {
		x *= FromBits((unsigned long long)(1023 - 1000) << 52);
		e += 1000;
	}
	return x * FromBits((unsigned long long)(1023 + e) << 52);
}

double Exp(double x)
{
	if (IsNan(x))
		return x;
	if (x > 709.782712893384)
		return Inf();
	if (x < -745.1332191019412)
		return 0;
	double k = Round(x / kLn2);
	double r = (x - k * kLn2Hi) - k * kLn2Lo;  // |r| <= ln2 / 2
	double p = 1.0 / 6227020800.0;            // 1/13!
	static const double kInvFact[] = {1.0 / 479001600.0, 1.0 / 39916800.0, 1.0 / 3628800.0,
					  1.0 / 362880.0,    1.0 / 40320.0,    1.0 / 5040.0,
					  1.0 / 720.0,       1.0 / 120.0,      1.0 / 24.0,
					  1.0 / 6.0,         0.5,              1.0,
					  1.0};
	for (double c : kInvFact)
		p = p * r + c;
	return Ldexp(p, (int)k);
}

double Ln(double x)
{
	if (IsNan(x) || x < 0)
		return Nan();
	if (x == 0)
		return -Inf();
	if (IsInf(x))
		return x;
	int k = 0;
	if (x < 2.2250738585072014e-308) {  // subnormal: scale up first
		x *= 18014398509481984.0;        // 2^54
		k = -54;
	}
	unsigned long long u = ToBits(x);
	k += (int)((u >> 52) & 0x7FF) - 1023;
	double mant = FromBits((u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull);  // [1, 2)
	if (mant > kSqrt2) {
		mant *= 0.5;
		k++;
	}
	double s = (mant - 1) / (mant + 1);  // |s| <= 0.1716
	double z = s * s;
	double p = 0;
	for (int n = 27; n >= 3; n -= 2)
		p = p * z + 1.0 / n;
	double lnm = 2 * s + 2 * s * z * p;
	return k * kLn2Hi + (k * kLn2Lo + lnm);
}

double Log10(double x)
{
	// Exact for powers of ten, so log(1000) = 3.
	for (int i = 0; i < 23; i++) {
		if (x == kPow10[i])
			return i;
		if (x == 1.0 / kPow10[i])  // 1e-3 is the double nearest to 1/1000
			return -i;
	}
	return Ln(x) / kLn10;
}

double Log2(double x)
{
	if (x > 0 && IsFinite(x)) {
		unsigned long long u = ToBits(x);
		if ((u & 0x000FFFFFFFFFFFFFull) == 0 && (u >> 52) != 0)
			return (double)((int)(u >> 52) - 1023);  // exact for powers of two
	}
	return Ln(x) / kLn2;
}

static double PowInt(double x, long long n)
{
	bool neg = n < 0;
	unsigned long long e = neg ? (unsigned long long)(-n) : (unsigned long long)n;
	double r = 1, b = x;
	while (e) {
		if (e & 1)
			r *= b;
		b *= b;
		e >>= 1;
	}
	return neg ? 1 / r : r;
}

double Pow(double x, double y)
{
	if (y == 0 || x == 1)
		return 1;
	if (IsNan(x) || IsNan(y))
		return Nan();
	if (Trunc(y) == y && Abs(y) < 1e15)
		return PowInt(x, (long long)y);
	if (x == 0)
		return y > 0 ? 0 : Inf();
	// Rational powers p/q with a small q: take the q-th root precisely, so
	// 8^(1/3) = 2, 32^(2/5) = 4, and odd roots of negatives are real:
	// (-8)^(1/3) = -2, (-8)^(2/3) = 4.
	for (int q = 2; q < 100; q++) {
		double p = Round(y * q);
		if (Abs(y * q - p) > 1e-14 * q || Abs(p) > 1e6)
			continue;
		if (x < 0 && q % 2 == 0)
			return Nan();  // even root of a negative number
		double a = Abs(x);
		double r;
		if (q == 2)
			r = Sqrt(a);  // correctly rounded: 2^0.5 is exactly sqrt(2)
		else {
			r = Exp(Ln(a) / q);
			if (r != 0 && IsFinite(r))
				r -= (PowInt(r, q) - a) / (q * PowInt(r, q - 1));  // Newton step
		}
		r = PowInt(r, (long long)p);
		return x < 0 && Fmod(p, 2.0) != 0 ? -r : r;
	}
	if (x < 0)
		return Nan();
	if (IsInf(y))
		return (x > 1) == (y > 0) ? Inf() : 0;
	return Exp(y * Ln(x));
}

double Cbrt(double x)
{
	if (x == 0 || !IsFinite(x))
		return x;
	double a = Abs(x);
	double y = Exp(Ln(a) / 3);
	y -= (y * y * y - a) / (3 * y * y);  // one Newton step: cbrt(27) = 3 exactly
	return x < 0 ? -y : y;
}

double Sin(double x)
{
	if (!IsFinite(x))
		return Nan();
	if (Abs(x) < 1e-9)
		return x;
	double r;
	int q = ReducePio2(x, &r);
	return SinQ(q, r);
}

double Cos(double x)
{
	if (!IsFinite(x))
		return Nan();
	double r;
	int q = ReducePio2(x, &r);
	return CosQ(q, r);
}

double Tan(double x)
{
	if (!IsFinite(x))
		return Nan();
	if (Abs(x) < 1e-9)
		return x;
	double r;
	int q = ReducePio2(x, &r);
	return TanQ(q, r);
}

// Degrees: reduce by whole multiples of 90 first (exact), then convert.
static int ReduceDeg(double x, double *r)
{
	double k = Round(x / 90);
	*r = (x - k * 90) * (kPi / 180);
	long long q = (long long)Fmod(k, 4.0);
	return (int)((q + 4) % 4);
}

double SinDeg(double x)
{
	if (!IsFinite(x))
		return Nan();
	double r;
	int q = ReduceDeg(x, &r);
	return SinQ(q, r);
}

double CosDeg(double x)
{
	if (!IsFinite(x))
		return Nan();
	double r;
	int q = ReduceDeg(x, &r);
	return CosQ(q, r);
}

double TanDeg(double x)
{
	if (!IsFinite(x))
		return Nan();
	double k = Round(x / 90), d = x - k * 90;
	if (d == 45 || d == -45) {  // tan(45 degrees) = 1 exactly
		bool odd = Fmod(k, 2.0) != 0;
		return (d > 0) != odd ? 1 : -1;
	}
	double r;
	int q = ReduceDeg(x, &r);
	return TanQ(q, r);
}

double Atan(double x)
{
	if (IsNan(x))
		return x;
	double a = Abs(x);
	double r = a <= 1 ? AtanUnit(a) : kPi / 2 - AtanUnit(1 / a);
	return x < 0 ? -r : r;
}

double Atan2(double y, double x)
{
	if (IsNan(x) || IsNan(y))
		return Nan();
	if (x == 0) {
		if (y == 0)
			return 0;
		return y > 0 ? kPi / 2 : -kPi / 2;
	}
	double a = Atan(Abs(y / x));
	if (x > 0)
		return y < 0 ? -a : a;
	return y < 0 ? a - kPi : kPi - a;
}

double Asin(double x)
{
	if (IsNan(x) || x < -1 || x > 1)
		return Nan();
	return Atan2(x, Sqrt((1 - x) * (1 + x)));
}

double Acos(double x)
{
	if (IsNan(x) || x < -1 || x > 1)
		return Nan();
	return Atan2(Sqrt((1 - x) * (1 + x)), x);
}

double Sinh(double x)
{
	double a = Abs(x);
	if (a < 0.5) {
		// Series: no cancellation for small x.
		double z = x * x;
		double term = x, sum = x;
		for (int n = 3; n <= 21; n += 2) {
			term *= z / ((n - 1) * n);
			sum += term;
		}
		return sum;
	}
	double e = Exp(a);
	double r = 0.5 * (e - 1 / e);
	return x < 0 ? -r : r;
}

double Cosh(double x)
{
	double e = Exp(Abs(x));
	return 0.5 * (e + 1 / e);
}

double Tanh(double x)
{
	double a = Abs(x);
	double r;
	if (a > 22)
		r = 1;
	else if (a < 0.5)
		r = Sinh(a) / Cosh(a);
	else {
		double e = Exp(-2 * a);
		r = (1 - e) / (1 + e);
	}
	return x < 0 ? -r : r;
}

double Gamma(double x)
{
	if (IsNan(x))
		return x;
	if (x == Floor(x) && x <= 0)
		return Nan();  // poles
	if (x < 0.5)        // reflection
		return kPi / (Sin(kPi * x) * Gamma(1 - x));
	if (x > 171.7)
		return Inf();
	// Lanczos approximation, g = 7, n = 9.
	static const double c[] = {0.99999999999980993,  676.5203681218851,     -1259.1392167224028,
				   771.32342877765313,   -176.61502916214059,   12.507343278686905,
				   -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7};
	x -= 1;
	double a = c[0];
	double t = x + 7.5;
	for (int i = 1; i < 9; i++)
		a += c[i] / (x + i);
	return Sqrt(2 * kPi) * Pow(t, x + 0.5) * Exp(-t) * a;
}

double Factorial(double n)
{
	if (IsNan(n))
		return n;
	if (n == Floor(n)) {
		if (n < 0)
			return Nan();
		if (n > 170)
			return Inf();
		double r = 1;
		for (int i = 2; i <= (int)n; i++)
			r *= i;
		return r;
	}
	return Gamma(n + 1);
}

} // namespace m
