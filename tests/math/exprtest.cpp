// Expression parser, evaluator and number formatting checks.
#include "../../src/core/expr.h"
#include "../../src/core/mathlib.h"
#include "../../src/core/numfmt.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
static expr::Env env;

// Evaluates text at x and compares the formatted result (10 digits).
static void Check(const char *text, const char *want, double x = 0)
{
	expr::Program p;
	expr::Error err;
	char got[96];
	if (!expr::Compile(text, &p, &err))
		snprintf(got, sizeof got, "error@%d: %s", err.pos, err.msg);
	else
		num::Format(expr::Eval(p, env, x), got, sizeof got);
	if (strcmp(got, want) != 0) {
		printf("%-24s = %-28s want %s  <-- FAIL\n", text, got, want);
		failures++;
	}
}

static void Fmt(double v, const char *want, bool plain = false)
{
	char got[64];
	if (plain)
		num::FormatPlain(v, got, sizeof got);
	else
		num::Format(v, got, sizeof got);
	if (strcmp(got, want) != 0) {
		printf("format %.17g = %s want %s  <-- FAIL\n", v, got, want);
		failures++;
	}
}

static void RoundTrip(double v)
{
	char s[64];
	num::FormatPlain(v, s, sizeof s, 17);
	double back = 0;
	bool neg = s[0] == '-';
	num::Parse(s + (neg ? 1 : 0), &back);
	if (neg)
		back = -back;
	// 17 digits keep the value to within a couple of units in the last place.
	if (m::Abs(back - v) > m::Abs(v) * 4.5e-16) {
		printf("round trip %.17g -> %s -> %.17g  <-- FAIL\n", v, s, back);
		failures++;
	}
}

int main()
{
	expr::ResetEnv(&env);
	Check("1+2*3", "7");
	Check("(1+2)*3", "9");
	Check("2^3^2", "512");
	Check("-2^2", "-4");
	Check("(-2)^2", "4");
	Check("2^-1", "0.5");
	Check("10/4", "2.5");
	Check("0.1+0.2", "0.3");
	Check("1/3", "0.3333333333");
	Check("2/3", "0.6666666667");
	Check("2x+1", "7", 3);
	Check("2x^2", "18", 3);
	Check("x(x+1)", "12", 3);
	Check("3(x-1)(x+1)", "24", 3);
	Check("1/2x", "1.5", 3);
	Check("2pi", "6.283185307");
	Check("pi", "3.141592654");
	Check("e", "2.718281828");
	Check("e^2", "7.389056099");
	Check("sin(pi)", "0");
	Check("sin(pi/6)", "0.5");
	Check("cos(pi/3)", "0.5");
	Check("tan(pi/4)", "1");
	Check("sqrt(16)", "4");
	Check("sqrt(2)", "1.414213562");
	Check("sqrt(-1)", "undefined");
	Check("1/0", "undefined");
	Check("|3-5|", "2");
	Check("2|x|", "6", -3);
	Check("|x-1|+|x+1|", "4", 2);
	Check("5!", "120");
	Check("3!!", "720");
	Check("log(1000)", "3");
	Check("log(8,2)", "3");
	Check("log(8, 2)", "3");
	Check("ln(e)", "1");
	Check("ln(0)", "undefined");
	Check("root(27,3)", "3");
	Check("(-8)^(1/3)", "-2");
	Check("ncr(5,2)", "10");
	Check("npr(5,2)", "20");
	Check("ncr(52,5)", "2598960");
	Check("gcd(12,18)", "6");
	Check("lcm(4,6)", "12");
	Check("mod(-7,3)", "2");
	Check("round(2.5)", "3");
	Check("round(pi,2)", "3.14");
	Check("floor(-2.5)", "-3");
	Check("2^100", "1.2676506\xC3\x97" "10\xC2\xB3\xE2\x81\xB0");
	Check("1E-7", "1\xC3\x97" "10\xE2\x81\xBB\xE2\x81\xB7");
	Check("6.02E23", "6.02\xC3\x97" "10\xC2\xB2\xC2\xB3");
	Check("123456789*10", "1234567890");
	Check("0.0001", "0.0001");
	Check("0.00001234", "1.234\xC3\x97" "10\xE2\x81\xBB\xE2\x81\xB5");
	Check("asin(2)", "undefined");
	Check("sinx", "error@0: sin needs brackets, like sin(30)");
	Check("xsin(x)", "0", 0);
	Check("sec(x)", "error@0: I don't know the word \"sec\"");
	Check("2+", "error@1: Something is missing after +");
	Check("(2+3", "error@0: A ) is missing");
	Check("2+3)", "error@3: There is a ) too many");
	Check("", "error@0: Type something to work out");
	Check("*3", "error@0: Something is missing before this sign");
	Check("1..2", "error@2: A number has two decimal points");
	Check("log(2,3,4)", "error@0: log needs one or two values, like log(8, 2)");
	Check("a=5", "error@1: = only works as \"a = 5\" or \"y1 = x^2\"");
	Check("2 $ 3", "error@2: I don't know this sign");
	env.vars['a' - 'a'] = 5;
	Check("2a", "10");
	Check("ab", "0");
	env.ans = 42;
	Check("ans/2", "21");
	env.degrees = true;
	Check("sin(30)", "0.5");
	Check("cos(60)", "0.5");
	Check("tan(45)", "1");
	Check("cos(90)", "0");
	Check("asin(1)", "90");
	Check("atan(1)", "45");
	env.degrees = false;
	// y1 .. y6
	expr::Program f1, f2, self;
	expr::Error err;
	expr::Compile("x^2", &f1, &err);
	expr::Compile("2y1+1", &f2, &err);
	expr::Compile("y3+1", &self, &err);
	env.fns[0] = &f1;
	env.fns[1] = &f2;
	env.fns[2] = &self;
	Check("y1(3)", "9");
	Check("y2(3)", "19");
	Check("y2", "9", 2);
	Check("y3(1)", "undefined");
	Check("y4(1)", "undefined");

	Fmt(0, "0");
	Fmt(-0.0, "0");
	Fmt(1, "1");
	Fmt(-1.5, "-1.5");
	Fmt(1e10, "1\xC3\x97" "10\xC2\xB9\xE2\x81\xB0");
	Fmt(9999999999, "9999999999");
	Fmt(99999999999.6, "1\xC3\x97" "10\xC2\xB9\xC2\xB9");
	Fmt(0.1 + 0.2, "0.3");
	Fmt(1.0 / 3, "0.3333333333");
	Fmt(m::Nan(), "undefined");
	Fmt(m::Inf(), "\xE2\x88\x9E");
	Fmt(1.5e-7, "1.5E-7", true);
	Fmt(123.25, "123.25", true);
	Fmt(1e300, "1E300", true);
	RoundTrip(0.1);
	RoundTrip(1.0 / 3);
	RoundTrip(m::kPi);
	RoundTrip(6.02214076e23);
	RoundTrip(-1.602176634e-19);
	RoundTrip(1e-300);
	RoundTrip(123456789.123);
	printf(failures ? "%d FAILED\n" : "all expression tests passed\n", failures);
	return failures ? 1 : 0;
}
