// Compares src/core/mathlib.cpp with the host's libm over many arguments and
// checks the exact cases a calculator user expects. Exit status 1 on failure.
#include "../../src/core/mathlib.h"
#include <cmath>
#include <cstdio>
#include <random>

static int failures = 0;

// Error in units of the last place (relative to the reference).
static double Ulps(double got, double want)
{
	if (std::isnan(want))
		return std::isnan(got) ? 0 : 1e9;
	if (std::isinf(want))
		return got == want ? 0 : 1e9;
	if (got == want)
		return 0;
	double ulp = std::nextafter(std::fabs(want), INFINITY) - std::fabs(want);
	return std::fabs(got - want) / ulp;
}

template <typename F, typename G>
static void Compare(const char *name, F mine, G ref, double lo, double hi, double maxUlps, bool logScale = false)
{
	std::mt19937_64 rng(42);
	std::uniform_real_distribution<double> d(lo, hi);
	double worst = 0, worstX = 0;
	for (int i = 0; i < 200000; i++) {
		double x = logScale ? std::exp(d(rng)) : d(rng);
		double u = Ulps(mine(x), ref(x));
		if (u > worst)
			worst = u, worstX = x;
	}
	bool ok = worst <= maxUlps;
	printf("%-8s [%g, %g]%s  worst %.2f ulp at %.17g %s\n", name, lo, hi, logScale ? " (exp)" : "", worst,
	       worstX, ok ? "" : "  <-- FAIL");
	if (!ok)
		failures++;
}

static void Exact(const char *what, double got, double want)
{
	bool ok = got == want || (std::isnan(got) && std::isnan(want));
	if (!ok) {
		printf("exact    %s = %.17g, want %.17g  <-- FAIL\n", what, got, want);
		failures++;
	}
}

int main()
{
	Compare("exp", m::Exp, (double (*)(double))std::exp, -700, 700, 2);
	Compare("exp", m::Exp, (double (*)(double))std::exp, -2, 2, 2);
	Compare("ln", m::Ln, (double (*)(double))std::log, -700, 700, 2, true);
	Compare("ln", m::Ln, (double (*)(double))std::log, 0.5, 2, 2);
	Compare("log10", m::Log10, (double (*)(double))std::log10, -50, 50, 3, true);
	Compare("sin", m::Sin, (double (*)(double))std::sin, -10, 10, 2);
	Compare("sin", m::Sin, (double (*)(double))std::sin, -1e5, 1e5, 2);
	Compare("cos", m::Cos, (double (*)(double))std::cos, -10, 10, 2);
	Compare("cos", m::Cos, (double (*)(double))std::cos, -1e5, 1e5, 2);
	Compare("tan", m::Tan, (double (*)(double))std::tan, -10, 10, 4);
	Compare("atan", m::Atan, (double (*)(double))std::atan, -20, 20, 4);
	Compare("atan", m::Atan, (double (*)(double))std::atan, -1, 1, 4);
	Compare("asin", m::Asin, (double (*)(double))std::asin, -1, 1, 4);
	Compare("acos", m::Acos, (double (*)(double))std::acos, -1, 1, 4);
	Compare("sinh", m::Sinh, (double (*)(double))std::sinh, -20, 20, 4);
	Compare("cosh", m::Cosh, (double (*)(double))std::cosh, -20, 20, 4);
	Compare("tanh", m::Tanh, (double (*)(double))std::tanh, -5, 5, 6);
	Compare("sqrt", m::Sqrt, (double (*)(double))std::sqrt, 0, 1e6, 0);
	Compare("cbrt", m::Cbrt, (double (*)(double))std::cbrt, -1e6, 1e6, 4);
	Compare("floor", m::Floor, (double (*)(double))std::floor, -1e6, 1e6, 0);
	Compare("ceil", m::Ceil, (double (*)(double))std::ceil, -1e6, 1e6, 0);
	Compare("round", m::Round, (double (*)(double))std::round, -1e6, 1e6, 0);
	Compare("gamma", m::Gamma, (double (*)(double))std::tgamma, 0.1, 20, 256);
	Compare("pow2.5", [](double x) { return m::Pow(x, 2.5); }, [](double x) { return std::pow(x, 2.5); }, 0, 100, 32);
	Compare("pow^x", [](double x) { return m::Pow(1.5, x); }, [](double x) { return std::pow(1.5, x); }, -100, 100, 64);
	Compare("pow^-7", [](double x) { return m::Pow(x, -7); }, [](double x) { return std::pow(x, -7); }, -3, 3, 8);
	Compare("fmod", [](double x) { return m::Fmod(x, 0.7); }, [](double x) { return std::fmod(x, 0.7); }, -100, 100, 0);

	// What a student expects to see.
	Exact("sin(pi)", m::Sin(m::kPi), 0);
	Exact("cos(pi/2)", m::Cos(m::kPi / 2), 0);
	Exact("sin(-2pi)", m::Sin(-2 * m::kPi), 0);
	Exact("tan(pi)", m::Tan(m::kPi), 0);
	Exact("tan(pi/2)", m::Tan(m::kPi / 2), m::Nan());
	Exact("sin(30 deg)", m::SinDeg(30), 0.49999999999999994);  // nearest to the rounded pi/6
	Exact("sin(180 deg)", m::SinDeg(180), 0);
	Exact("cos(90 deg)", m::CosDeg(90), 0);
	Exact("cos(-270 deg)", m::CosDeg(-270), 0);
	Exact("sin(90 deg)", m::SinDeg(90), 1);
	Exact("tan(45 deg)", m::TanDeg(45), 1);
	Exact("tan(135 deg)", m::TanDeg(135), -1);
	Exact("tan(-45 deg)", m::TanDeg(-45), -1);
	Exact("tan(225 deg)", m::TanDeg(225), 1);
	Exact("tan(90 deg)", m::TanDeg(90), m::Nan());
	Exact("log10(1000)", m::Log10(1000), 3);
	Exact("log10(1e-3)", m::Log10(1e-3), std::log10(1e-3));
	Exact("log2(1024)", m::Log2(1024), 10);
	Exact("ln(1)", m::Ln(1), 0);
	Exact("exp(0)", m::Exp(0), 1);
	Exact("2^10", m::Pow(2, 10), 1024);
	Exact("(-2)^3", m::Pow(-2, 3), -8);
	Exact("(-8)^(1/3)", m::Pow(-8, 1.0 / 3), -2);
	Exact("(-8)^(2/3)", m::Round(m::Pow(-8, 2.0 / 3) * 1e12) / 1e12, 4);
	Exact("(-4)^0.5", m::Pow(-4, 0.5), m::Nan());
	Exact("8^(1/3)", m::Pow(8, 1.0 / 3), 2);
	Exact("32^(2/5)", m::Pow(32, 0.4), 4);
	Exact("27^(-2/3)", m::Pow(27, -2.0 / 3), 1.0 / 9);
	Exact("2^0.5", m::Pow(2, 0.5), std::sqrt(2.0));
	Exact("0^0", m::Pow(0, 0), 1);
	Exact("0^-1", m::Pow(0, -1), m::Inf());
	Exact("cbrt(27)", m::Cbrt(27), 3);
	Exact("cbrt(-64)", m::Cbrt(-64), -4);
	Exact("sqrt(-1)", m::Sqrt(-1), m::Nan());
	Exact("5!", m::Factorial(5), 120);
	Exact("20!", m::Factorial(20), 2432902008176640000.0);
	Exact("(-1)!", m::Factorial(-1), m::Nan());
	Exact("round(2.5)", m::Round(2.5), 3);
	Exact("round(-2.5)", m::Round(-2.5), -3);
	Exact("round(0.49999999999999994)", m::Round(0.49999999999999994), 0);
	Exact("floor(-0.5)", m::Floor(-0.5), -1);
	Exact("fmod(7,3)", m::Fmod(7, 3), 1);
	Exact("fmod(-7,3)", m::Fmod(-7, 3), -1);
	Exact("exp(710)", m::Exp(710), m::Inf());
	Exact("ln(0)", m::Ln(0), -m::Inf());
	Exact("ln(-1)", m::Ln(-1), m::Nan());
	Exact("asin(2)", m::Asin(2), m::Nan());
	Exact("atan2(1,-1)", m::Atan2(1, -1), std::atan2(1, -1));
	Exact("atan2(-1,-1)", m::Atan2(-1, -1), std::atan2(-1, -1));
	printf(failures ? "%d FAILED\n" : "all math tests passed\n", failures);
	return failures ? 1 : 0;
}
