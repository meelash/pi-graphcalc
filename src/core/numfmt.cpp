#include "numfmt.h"
#include "mathlib.h"

namespace num {

static const double kExact[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
				1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

double Pow10(int e)
{
	if (e >= 0 && e <= 22)
		return kExact[e];
	if (e < 0 && e >= -22)
		return 1 / kExact[-e];
	double r = 1;
	int a = e < 0 ? -e : e;
	while (a > 22) {
		r *= 1e22;
		a -= 22;
	}
	r *= kExact[a];
	return e < 0 ? 1 / r : r;
}

// Decimal digits of a > 0: digits[0..sig-1] and the exponent of digits[0].
static void Digits(double a, int sig, char *digits, int *exp10)
{
	int e = (int)m::Floor(m::Log10(a));
	unsigned long long top = 1;
	for (int i = 1; i < sig; i++)
		top *= 10;  // 10^(sig-1)
	unsigned long long d = 0;
	for (int tries = 0; tries < 3; tries++) {
		// Scale so the digits we want are the integer part. Dividing by an
		// exact power of ten keeps the error to one rounding.
		int shift = sig - 1 - e;
		double scaled = shift > 300   ? a * 1e300 * Pow10(shift - 300)  // tiny numbers
				: shift >= 0 ? a * Pow10(shift)
					     : a / Pow10(-shift);
		d = (unsigned long long)m::Round(scaled);
		if (d >= top * 10)
			e++;  // rounded up to one more digit
		else if (d < top)
			e--;
		else
			break;
	}
	if (d >= top * 10) {
		d /= 10;
		e++;
	}
	for (int i = sig - 1; i >= 0; i--) {
		digits[i] = (char)('0' + d % 10);
		d /= 10;
	}
	*exp10 = e;
}

static void Put(char *out, int cap, int &n, const char *s)
{
	while (*s && n < cap - 1)
		out[n++] = *s++;
	out[n] = 0;
}

static void PutChar(char *out, int cap, int &n, char c)
{
	char s[2] = {c, 0};
	Put(out, cap, n, s);
}

static void FormatImpl(double v, char *out, int cap, int sig, bool plain)
{
	int n = 0;
	out[0] = 0;
	if (m::IsNan(v)) {
		Put(out, cap, n, plain ? "nan" : "undefined");
		return;
	}
	if (m::IsInf(v)) {
		Put(out, cap, n, v > 0 ? (plain ? "inf" : "\xE2\x88\x9E") : (plain ? "-inf" : "-\xE2\x88\x9E"));
		return;
	}
	if (v == 0) {
		Put(out, cap, n, "0");
		return;
	}
	sig = Clamp(sig, 1, 17);
	char d[18];
	int e;
	Digits(v < 0 ? -v : v, sig, d, &e);
	int nd = sig;
	while (nd > 1 && d[nd - 1] == '0')
		nd--;
	if (v < 0)
		PutChar(out, cap, n, '-');
	bool fixed = e >= -4 && e < 10 && (plain ? e < sig : true);
	if (fixed) {
		if (e >= 0) {
			for (int i = 0; i <= e; i++)
				PutChar(out, cap, n, i < nd ? d[i] : '0');
			if (nd > e + 1) {
				PutChar(out, cap, n, '.');
				for (int i = e + 1; i < nd; i++)
					PutChar(out, cap, n, d[i]);
			}
		} else {
			Put(out, cap, n, "0.");
			for (int i = 0; i < -e - 1; i++)
				PutChar(out, cap, n, '0');
			for (int i = 0; i < nd; i++)
				PutChar(out, cap, n, d[i]);
		}
		return;
	}
	PutChar(out, cap, n, d[0]);
	if (nd > 1) {
		PutChar(out, cap, n, '.');
		for (int i = 1; i < nd; i++)
			PutChar(out, cap, n, d[i]);
	}
	char exps[8];
	int en = 0, a = e < 0 ? -e : e;
	do {
		exps[en++] = (char)('0' + a % 10);
		a /= 10;
	} while (a);
	if (plain) {
		PutChar(out, cap, n, 'E');
		if (e < 0)
			PutChar(out, cap, n, '-');
		while (en)
			PutChar(out, cap, n, exps[--en]);
		return;
	}
	// "×10" and the exponent in superscript digits.
	static const char *const kSup[] = {"\xE2\x81\xB0", "\xC2\xB9",     "\xC2\xB2",     "\xC2\xB3",
					   "\xE2\x81\xB4", "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7",
					   "\xE2\x81\xB8", "\xE2\x81\xB9"};
	Put(out, cap, n, "\xC3\x97" "10");
	if (e < 0)
		Put(out, cap, n, "\xE2\x81\xBB");
	while (en)
		Put(out, cap, n, kSup[exps[--en] - '0']);
}

void Format(double v, char *out, int cap, int sig) { FormatImpl(v, out, cap, sig, false); }

void FormatPlain(double v, char *out, int cap, int sig) { FormatImpl(v, out, cap, sig, true); }

int Parse(const char *s, double *out)
{
	const char *p = s;
	unsigned long long mant = 0;
	int digits = 0, exp10 = 0;
	bool any = false;
	while (*p >= '0' && *p <= '9') {
		any = true;
		if (digits < 19) {
			mant = mant * 10 + (unsigned)(*p - '0');
			if (mant)
				digits++;
		} else
			exp10++;
		p++;
	}
	if (*p == '.') {
		p++;
		while (*p >= '0' && *p <= '9') {
			any = true;
			if (digits < 19) {
				mant = mant * 10 + (unsigned)(*p - '0');
				if (mant)
					digits++;
				exp10--;
			}
			p++;
		}
	}
	if (!any)
		return 0;
	// Exponent: "E", optional sign, digits (only if digits follow).
	if (*p == 'E') {
		const char *q = p + 1;
		bool neg = false;
		if (*q == '+' || *q == '-')
			neg = *q++ == '-';
		if (*q >= '0' && *q <= '9') {
			int e = 0;
			while (*q >= '0' && *q <= '9') {
				if (e < 10000)
					e = e * 10 + (*q - '0');
				q++;
			}
			exp10 += neg ? -e : e;
			p = q;
		}
	}
	double v = (double)mant;
	// One correctly rounded operation when both parts are exact.
	if (mant < (1ull << 53) && exp10 >= -22 && exp10 <= 22)
		v = exp10 >= 0 ? v * kExact[exp10] : v / kExact[-exp10];
	else if (exp10 < -300) {
		v = v * Pow10(-300) * Pow10(exp10 + 300);
	} else
		v = exp10 >= 0 ? v * Pow10(exp10) : v / Pow10(-exp10);
	*out = v;
	return (int)(p - s);
}

} // namespace num
