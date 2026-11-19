// Expressions as students type them: 2x^2 - 3x + 1, sin(30), |x - 2|, 5!,
// y1(3), log(8, 2). Compiled once to a small stack program, then evaluated
// quickly for every point of a graph.
#pragma once
#include "base.h"

namespace expr {

static const int kFunctions = 6;  // y1 .. y6
static const int kMaxCode = 160;
static const int kMaxDepth = 8;   // y1 may use y2 ..., but not endlessly

enum Op : u8 {
	OpNum,     // push num
	OpX,       // push x
	OpVar,     // push variable arg (0 = a ... 25 = z)
	OpAns,     // push the last answer
	OpNeg,
	OpAdd,
	OpSub,
	OpMul,
	OpDiv,
	OpPow,
	OpFact,    // n!
	OpFunc,    // built-in function arg with nargs arguments
	OpCallY,   // y<arg+1>(top of stack)
};

enum Func : u8 {
	FSin, FCos, FTan, FAsin, FAcos, FAtan, FSinh, FCosh, FTanh, FSqrt, FCbrt, FAbs, FLn, FLog,
	FExp, FFloor, FCeil, FRound, FSign, FRoot, FNcr, FNpr, FMin, FMax, FMod, FGcd, FLcm, FCount
};

struct Instr
{
	u8 op;
	u8 arg;
	u8 nargs;
	double num;
};

struct Program
{
	Instr code[kMaxCode];
	int len;
	bool usesX;
	u32 usesY;  // bit n: refers to y(n+1)
};

struct Env
{
	double vars[26];  // a .. z (e is Euler's number, x is the graph variable)
	double ans;
	bool degrees;
	const Program *fns[kFunctions];  // null when empty or invalid
};

struct Error
{
	int pos;  // byte offset in the text where the problem is
	char msg[80];
};

void ResetEnv(Env *env);

// Compiles text; false with err filled in when it can't be understood.
bool Compile(const char *text, Program *out, Error *err);

// Evaluates at x. Results that don't exist (sqrt(-1), 1/0, tan(90)) are NaN
// or infinite; numbers too big are infinite.
double Eval(const Program &p, const Env &env, double x, int depth = 0);

// Built-in function names, for the help screen ("sin", ...).
const char *FuncName(Func f);

} // namespace expr
