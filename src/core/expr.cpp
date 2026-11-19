#include "expr.h"
#include "mathlib.h"
#include "numfmt.h"

namespace expr {

namespace {

struct FuncInfo
{
	const char *name;
	Func f;
	u8 minArgs, maxArgs;
};

// Longer names first where one is a prefix of another (asin before a...).
const FuncInfo kFuncs[] = {
	{"arcsin", FAsin, 1, 1}, {"arccos", FAcos, 1, 1}, {"arctan", FAtan, 1, 1}, {"asin", FAsin, 1, 1},
	{"acos", FAcos, 1, 1},   {"atan", FAtan, 1, 1},   {"sinh", FSinh, 1, 1},   {"cosh", FCosh, 1, 1},
	{"tanh", FTanh, 1, 1},   {"sin", FSin, 1, 1},     {"cos", FCos, 1, 1},     {"tan", FTan, 1, 1},
	{"sqrt", FSqrt, 1, 1},   {"cbrt", FCbrt, 1, 1},   {"abs", FAbs, 1, 1},     {"ln", FLn, 1, 1},
	{"log", FLog, 1, 2},     {"exp", FExp, 1, 1},     {"floor", FFloor, 1, 1}, {"ceil", FCeil, 1, 1},
	{"round", FRound, 1, 2}, {"sign", FSign, 1, 1},   {"root", FRoot, 2, 2},   {"ncr", FNcr, 2, 2},
	{"npr", FNpr, 2, 2},     {"min", FMin, 2, 2},     {"max", FMax, 2, 2},     {"mod", FMod, 2, 2},
	{"gcd", FGcd, 2, 2},     {"lcm", FLcm, 2, 2},
};

char Lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
bool IsLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Does the text at s start with name (case-insensitive)?
bool StartsWith(const char *s, const char *name)
{
	for (; *name; s++, name++)
		if (Lower(*s) != *name)
			return false;
	return true;
}

class Parser
{
public:
	Parser(const char *text, Program *out, Error *err) : m_s(text), m_p(0), m_out(out), m_err(err), m_abs(0) {}

	bool Run()
	{
		m_out->len = 0;
		m_out->usesX = false;
		m_out->usesY = 0;
		m_err->pos = -1;
		m_err->msg[0] = 0;
		Skip();
		if (!m_s[m_p])
			return Fail(0, "Type something to work out");
		if (!Sum())
			return false;
		Skip();
		if (m_s[m_p]) {
			if (m_s[m_p] == ')')
				return Fail(m_p, "There is a ) too many");
			if (m_s[m_p] == '=')
				return Fail(m_p, "= only works as \"a = 5\" or \"y1 = x^2\"");
			if (m_s[m_p] == ',')
				return Fail(m_p, "A comma only goes between a function's values");
			if (!IsDigit(m_s[m_p]) && !IsLetter(m_s[m_p]) && m_s[m_p] != '.' && m_s[m_p] != '(' && m_s[m_p] != '|')
				return Fail(m_p, "I don't know this sign");
			return Fail(m_p, "I don't understand this part");
		}
		return true;
	}

private:
	bool Fail(int pos, const char *msg)
	{
		if (m_err->pos < 0) {
			m_err->pos = pos;
			StrCopy(m_err->msg, msg, sizeof m_err->msg);
		}
		return false;
	}

	void Skip()
	{
		while (m_s[m_p] == ' ' || m_s[m_p] == '\t')
			m_p++;
	}

	bool Emit(u8 op, u8 arg = 0, u8 nargs = 0, double num = 0)
	{
		if (m_out->len >= kMaxCode)
			return Fail(m_p, "This is too long for me");
		Instr &i = m_out->code[m_out->len++];
		i.op = op;
		i.arg = arg;
		i.nargs = nargs;
		i.num = num;
		return true;
	}

	// Can a value start here (for implicit multiplication: 2x, 3(x+1), x sin(x))?
	bool StartsValue()
	{
		Skip();
		char c = m_s[m_p];
		if (c == '|')
			return m_abs == 0;  // inside |...| a bar closes it
		// UTF-8 pi
		if ((u8)c == 0xCF && (u8)m_s[m_p + 1] == 0x80)
			return true;
		return IsDigit(c) || c == '.' || IsLetter(c) || c == '(';
	}

	bool Sum()
	{
		if (!Product())
			return false;
		for (;;) {
			Skip();
			char c = m_s[m_p];
			if (c != '+' && c != '-')
				return true;
			int at = m_p++;
			Skip();
			if (!m_s[m_p] || m_s[m_p] == ')')
				return Fail(at, c == '+' ? "Something is missing after +" : "Something is missing after -");
			if (!Product() || !Emit(c == '+' ? OpAdd : OpSub))
				return false;
		}
	}

	bool Product()
	{
		if (!Unary())
			return false;
		for (;;) {
			Skip();
			char c = m_s[m_p];
			bool times = c == '*' || ((u8)c == 0xC3 && (u8)m_s[m_p + 1] == 0x97);   // * or ×
			bool divide = c == '/' || ((u8)c == 0xC3 && (u8)m_s[m_p + 1] == 0xB7);  // / or ÷
			if (times || divide) {
				int at = m_p;
				m_p += c == '*' || c == '/' ? 1 : 2;
				Skip();
				if (!m_s[m_p] || m_s[m_p] == ')')
					return Fail(at, "Something is missing after this sign");
				if (!Unary() || !Emit(times ? OpMul : OpDiv))
					return false;
			} else if (StartsValue()) {
				// Implicit multiplication binds like *, so 1/2x = (1/2)x.
				if (!Power() || !Emit(OpMul))
					return false;
			} else
				return true;
		}
	}

	bool Unary()
	{
		Skip();
		if (m_s[m_p] == '-') {
			int at = m_p++;
			Skip();
			if (!m_s[m_p])
				return Fail(at, "Something is missing after -");
			return Unary() && Emit(OpNeg);  // -2^2 = -4
		}
		if (m_s[m_p] == '+') {
			m_p++;
			return Unary();
		}
		return Power();
	}

	bool Power()
	{
		if (!Postfix())
			return false;
		Skip();
		if (m_s[m_p] == '^') {
			int at = m_p++;
			Skip();
			if (!m_s[m_p] || m_s[m_p] == ')')
				return Fail(at, "Something is missing after ^");
			return Unary() && Emit(OpPow);  // right-associative: 2^3^2 = 2^9
		}
		return true;
	}

	bool Postfix()
	{
		if (!Primary())
			return false;
		for (;;) {
			Skip();
			if (m_s[m_p] == '!') {
				m_p++;
				if (!Emit(OpFact))
					return false;
			} else
				return true;
		}
	}

	bool Args(int *count, int at)
	{
		// at '('
		m_p++;
		int n = 0;
		int saveAbs = m_abs;
		m_abs = 0;
		Skip();
		if (m_s[m_p] == ')')
			return Fail(m_p, "Put a value inside the brackets");
		for (;;) {
			if (!Sum())
				return false;
			n++;
			Skip();
			if (m_s[m_p] == ',') {
				m_p++;
				continue;
			}
			if (m_s[m_p] == ')') {
				m_p++;
				break;
			}
			if (!m_s[m_p])
				return Fail(at, "A ) is missing");
			return Fail(m_p, "I don't understand this part");
		}
		m_abs = saveAbs;
		*count = n;
		return true;
	}

	bool Primary()
	{
		Skip();
		int at = m_p;
		char c = m_s[m_p];
		if (IsDigit(c) || c == '.') {
			double v;
			int used = num::Parse(m_s + m_p, &v);
			if (!used)
				return Fail(at, "A number is not written right");
			m_p += used;
			if (m_s[m_p] == '.')
				return Fail(m_p, "A number has two decimal points");
			return Emit(OpNum, 0, 0, v);
		}
		if (c == '(') {
			m_p++;
			int saveAbs = m_abs;
			m_abs = 0;
			Skip();
			if (m_s[m_p] == ')')
				return Fail(at, "Put something inside the brackets");
			if (!Sum())
				return false;
			m_abs = saveAbs;
			Skip();
			if (m_s[m_p] != ')')
				return Fail(at, "A ) is missing");
			m_p++;
			return true;
		}
		if (c == '|') {
			m_p++;
			m_abs++;
			Skip();
			if (m_s[m_p] == '|')
				return Fail(at, "Put something between the | bars");
			if (!Sum())
				return false;
			m_abs--;
			Skip();
			if (m_s[m_p] != '|')
				return Fail(at, "The closing | is missing");
			m_p++;
			return Emit(OpFunc, FAbs, 1);
		}
		if ((u8)c == 0xCF && (u8)m_s[m_p + 1] == 0x80) {  // π
			m_p += 2;
			return Emit(OpNum, 0, 0, m::kPi);
		}
		if (IsLetter(c))
			return Word();
		if (!c)
			return Fail(at, "Something is missing at the end");
		if (c == ')')
			return Fail(at, "Something is missing before )");
		if (c == '*' || c == '/' || c == '^' || c == '!')
			return Fail(at, "Something is missing before this sign");
		if (c == ',')
			return Fail(at, "A comma only goes between a function's values");
		if (c == '=')
			return Fail(at, "= only works as \"a = 5\" or \"y1 = x^2\"");
		return Fail(at, "I don't know this sign");
	}

	bool Word()
	{
		int at = m_p;
		// A run of 3+ letters with no name I know inside is a mistake (sec,
		// cot, sinx is fine: it holds "sin"), not a product of variables.
		if (at == 0 || !IsLetter(m_s[at - 1])) {
			int run = 0;
			while (IsLetter(m_s[at + run]))
				run++;
			if (run >= 3 && !KnownInside(at, run)) {
				static char msg[80];
				StrCopy(msg, "I don't know the word \"", sizeof msg);
				int n = StrLen(msg);
				for (int i = 0; i < run && n < 60; i++)
					msg[n++] = m_s[at + i];
				msg[n] = 0;
				StrAppend(msg, "\"", sizeof msg);
				return Fail(at, msg);
			}
		}
		// Built-in functions: need brackets.
		for (const FuncInfo &f : kFuncs) {
			if (!StartsWith(m_s + m_p, f.name))
				continue;
			int len = StrLen(f.name);
			m_p += len;
			Skip();
			if (m_s[m_p] != '(') {
				static char msg[80];
				StrCopy(msg, f.name, sizeof msg);
				StrAppend(msg, " needs brackets, like ", sizeof msg);
				StrAppend(msg, f.name, sizeof msg);
				StrAppend(msg, f.maxArgs == 2 ? "(8, 2)" : "(30)", sizeof msg);
				return Fail(at, msg);
			}
			int n;
			if (!Args(&n, m_p))
				return false;
			if (n < f.minArgs || n > f.maxArgs) {
				static char msg[80];
				StrCopy(msg, f.name, sizeof msg);
				StrAppend(msg,
					  f.minArgs == 2   ? " needs two values, like "
					  : f.maxArgs == 2 ? " needs one or two values, like "
							   : " needs one value, like ",
					  sizeof msg);
				StrAppend(msg, f.name, sizeof msg);
				StrAppend(msg, f.maxArgs == 2 ? "(8, 2)" : "(30)", sizeof msg);
				return Fail(at, msg);
			}
			return Emit(OpFunc, f.f, (u8)n);
		}
		if (StartsWith(m_s + m_p, "pi") && !IsLetter(m_s[m_p + 2])) {
			m_p += 2;
			return Emit(OpNum, 0, 0, m::kPi);
		}
		if (StartsWith(m_s + m_p, "ans")) {
			m_p += 3;
			return Emit(OpAns);
		}
		// y1 .. y6, as y1(3) or just y1 (= y1(x)).
		if (Lower(m_s[m_p]) == 'y' && m_s[m_p + 1] >= '1' && m_s[m_p + 1] <= '0' + kFunctions) {
			u8 n = (u8)(m_s[m_p + 1] - '1');
			m_p += 2;
			m_out->usesY |= 1u << n;
			Skip();
			if (m_s[m_p] == '(') {
				int count;
				if (!Args(&count, m_p))
					return false;
				if (count != 1)
					return Fail(at, "Use y1 with one value, like y1(3)");
			} else {
				m_out->usesX = true;
				if (!Emit(OpX))
					return false;
			}
			return Emit(OpCallY, n);
		}
		// Single letters: one variable per letter, so "ab" is a times b.
		char c = Lower(m_s[m_p++]);
		if (c == 'e')
			return Emit(OpNum, 0, 0, m::kE);
		if (c == 'x') {
			m_out->usesX = true;
			return Emit(OpX);
		}
		return Emit(OpVar, (u8)(c - 'a'));
	}

	bool KnownInside(int at, int run)
	{
		for (int i = 0; i < run; i++) {
			for (const FuncInfo &f : kFuncs)
				if (StartsWith(m_s + at + i, f.name))
					return true;
			if (StartsWith(m_s + at + i, "pi") || StartsWith(m_s + at + i, "ans"))
				return true;
		}
		return false;
	}

	const char *m_s;
	int m_p;
	Program *m_out;
	Error *m_err;
	int m_abs;  // nesting of |...|
};

double Deg(const Env &env, double rad) { return env.degrees ? rad * (180 / m::kPi) : rad; }

bool Whole(double v) { return m::IsFinite(v) && m::Floor(v) == v; }

double Gcd(double a, double b)
{
	if (!Whole(a) || !Whole(b))
		return m::Nan();
	a = m::Abs(a), b = m::Abs(b);
	while (b != 0) {
		double t = m::Fmod(a, b);
		a = b;
		b = t;
	}
	return a;
}

double Choose(double n, double r, bool perm)
{
	if (!Whole(n) || !Whole(r) || r < 0 || n < 0 || r > n)
		return m::Nan();
	if (!perm && r > n - r)
		r = n - r;
	double v = 1;
	for (double i = 1; i <= r; i++) {
		if (perm)
			v *= n - r + i;
		else
			v = v * (n - r + i) / i;  // stays a whole number at each step
		if (m::IsInf(v))
			break;
	}
	return m::Round(v);
}

double CallFunc(Func f, const double *a, int n, const Env &env)
{
	double x = a[0];
	switch (f) {
	case FSin: return env.degrees ? m::SinDeg(x) : m::Sin(x);
	case FCos: return env.degrees ? m::CosDeg(x) : m::Cos(x);
	case FTan: return env.degrees ? m::TanDeg(x) : m::Tan(x);
	case FAsin: return Deg(env, m::Asin(x));
	case FAcos: return Deg(env, m::Acos(x));
	case FAtan: return Deg(env, m::Atan(x));
	case FSinh: return m::Sinh(x);
	case FCosh: return m::Cosh(x);
	case FTanh: return m::Tanh(x);
	case FSqrt: return m::Sqrt(x);
	case FCbrt: return m::Cbrt(x);
	case FAbs: return m::Abs(x);
	case FLn: return x > 0 ? m::Ln(x) : m::Nan();
	case FLog:
		if (n == 2) {
			double b = a[1];
			if (x <= 0 || b <= 0 || b == 1)
				return m::Nan();
			if (b == 2)
				return m::Log2(x);
			if (b == 10)
				return m::Log10(x);
			return m::Ln(x) / m::Ln(b);
		}
		return x > 0 ? m::Log10(x) : m::Nan();
	case FExp: return m::Exp(x);
	case FFloor: return m::Floor(x);
	case FCeil: return m::Ceil(x);
	case FRound:
		if (n == 2) {
			if (!Whole(a[1]) || a[1] < 0 || a[1] > 15)
				return m::Nan();
			double s = num::Pow10((int)a[1]);
			return m::Round(x * s) / s;
		}
		return m::Round(x);
	case FSign: return x > 0 ? 1 : x < 0 ? -1 : 0;
	case FRoot:  // root(x, n): the n-th root
		if (a[1] == 0)
			return m::Nan();
		if (a[1] == 2)
			return m::Sqrt(x);
		if (a[1] == 3)
			return m::Cbrt(x);
		return m::Pow(x, 1 / a[1]);
	case FNcr: return Choose(x, a[1], false);
	case FNpr: return Choose(x, a[1], true);
	case FMin: return x < a[1] ? x : a[1];
	case FMax: return x > a[1] ? x : a[1];
	case FMod: {
		double r = m::Fmod(x, a[1]);
		if (r != 0 && (r < 0) != (a[1] < 0))
			r += a[1];
		return r;
	}
	case FGcd: return Gcd(x, a[1]);
	case FLcm: {
		double g = Gcd(x, a[1]);
		if (m::IsNan(g))
			return g;
		return g == 0 ? 0 : m::Abs(x / g * a[1]);
	}
	case FCount: break;
	}
	return m::Nan();
}

} // namespace

void ResetEnv(Env *env)
{
	for (int i = 0; i < 26; i++)
		env->vars[i] = 0;
	env->ans = 0;
	env->degrees = false;
	for (int i = 0; i < kFunctions; i++)
		env->fns[i] = 0;
}

bool Compile(const char *text, Program *out, Error *err)
{
	Parser p(text, out, err);
	return p.Run();
}

double Eval(const Program &p, const Env &env, double x, int depth)
{
	double st[64];
	int sp = 0;
	for (int i = 0; i < p.len; i++) {
		const Instr &in = p.code[i];
		if (sp > 60)
			return m::Nan();
		switch (in.op) {
		case OpNum: st[sp++] = in.num; break;
		case OpX: st[sp++] = x; break;
		case OpVar: st[sp++] = env.vars[in.arg]; break;
		case OpAns: st[sp++] = env.ans; break;
		case OpNeg: st[sp - 1] = -st[sp - 1]; break;
		case OpAdd: sp--, st[sp - 1] += st[sp]; break;
		case OpSub: sp--, st[sp - 1] -= st[sp]; break;
		case OpMul: sp--, st[sp - 1] *= st[sp]; break;
		case OpDiv:
			sp--;
			// Division by zero has no answer (rather than infinity).
			st[sp - 1] = st[sp] == 0 ? m::Nan() : st[sp - 1] / st[sp];
			break;
		case OpPow: sp--, st[sp - 1] = m::Pow(st[sp - 1], st[sp]); break;
		case OpFact: st[sp - 1] = m::Factorial(st[sp - 1]); break;
		case OpFunc:
			sp -= in.nargs;
			st[sp] = CallFunc((Func)in.arg, &st[sp], in.nargs, env);
			sp++;
			break;
		case OpCallY: {
			const Program *f = env.fns[in.arg];
			if (!f || depth >= kMaxDepth)
				st[sp - 1] = m::Nan();
			else
				st[sp - 1] = Eval(*f, env, st[sp - 1], depth + 1);
			break;
		}
		}
	}
	return sp == 1 ? st[0] : m::Nan();
}

const char *FuncName(Func f)
{
	for (const FuncInfo &i : kFuncs)
		if (i.f == f && !StartsWith(i.name, "arc"))
			return i.name;
	return "?";
}

} // namespace expr
