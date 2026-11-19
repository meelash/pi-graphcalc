#include "app.h"
#include "mathlib.h"
#include "numfmt.h"
#include "theme.h"

static const char kStateFile[] = "graphcalc/state.txt";
static const int kLeft = 1 << 20;  // m_leaving once ReturnToMenu() was called

static const char *const kTabNames[ScrCount] = {"Calculate", "Functions", "Graph", "Table", "Settings", "Help"};

App::App(Platform *platform) : m_platform(platform) {}

void App::Init()
{
	static u32 plot[W * kPlotH];
	static u8 cover[W * kPlotH];
	m_plot = plot;
	m_cover = cover;
	m_now = m_blinkStart = m_lastChange = m_platform->Millis();
	m_dirty = true;
	m_saveDirty = false;
	m_blinkPhase = 0;
	expr::ResetEnv(&m_env);
	for (int i = 0; i < expr::kFunctions; i++) {
		m_fn[i].edit.Clear();
		m_fn[i].on = true;
		Recompile(i);
	}
	m_input.Clear();
	m_histCount = 0;
	m_recall = -1;
	m_fnSel = 0;
	m_traceFn = -1;
	m_traceX = 0;
	m_mark = false;
	m_msg[0] = 0;
	m_msgError = false;
	m_gotoOpen = false;
	m_plotValid = false;
	m_tblStart = 0;
	m_tblStep = 1;
	m_tblSel = m_tblTop = 0;
	m_setSel = 0;
	m_menuConfirm = false;
	m_leaving = -1;
	m_diag = false;
	m_diagScroll = 0;
	StandardView();
	Load();
	m_screen = ScrCalc;
}

const char *App::StatusText() const { return m_msg; }

void App::Go(Screen s)
{
	if (m_screen == ScrSettings && s != ScrSettings && m_setSel <= SetTblStep)
		ApplySetting(m_setSel);
	m_screen = s;
	m_msg[0] = 0;
	m_gotoOpen = false;
	m_dirty = true;
	if (s == ScrGraph && m_traceFn < 0)
		FirstTrace();
	if (s == ScrSettings)
		LoadSettingsText();
	if (m_saveDirty)
		Save();
}

void App::Recompile(int i)
{
	Fn &f = m_fn[i];
	f.ok = !f.edit.Empty() && expr::Compile(f.edit.text, &f.prog, &f.err);
	if (f.edit.Empty())
		f.err.msg[0] = 0;
	m_env.fns[i] = f.ok ? &f.prog : 0;
	m_plotValid = false;
}

double App::EvalText(const char *text, bool *ok, char *err, int errCap)
{
	static expr::Program p;
	expr::Error e;
	if (!expr::Compile(text, &p, &e)) {
		*ok = false;
		StrCopy(err, e.msg, errCap);
		return 0;
	}
	double v = expr::Eval(p, m_env, m_env.vars['x' - 'a']);
	*ok = m::IsFinite(v);
	if (!*ok)
		StrCopy(err, "That has no value", errCap);
	return v;
}

// ---------------------------------------------------------------------------
// Saving: a plain text file a parent can read.

static void AppendLine(char *buf, int cap, int &len, const char *a, const char *b = "", const char *c = "")
{
	const char *parts[] = {a, b, c, "\n"};
	for (const char *p : parts)
		while (*p && len < cap - 1)
			buf[len++] = *p++;
	buf[len] = 0;
}

void App::Save()
{
	m_saveDirty = false;
	static char buf[16384];
	int len = 0;
	buf[0] = 0;
	char a[40], b[40], c2[40], d[40], line[200];
	AppendLine(buf, sizeof buf, len, "# Graphing Calculator: your functions, settings and work.");
	AppendLine(buf, sizeof buf, len, "angle=", m_env.degrees ? "deg" : "rad");
	num::FormatPlain(m_xmin, a, sizeof a, 17);
	num::FormatPlain(m_xmax, b, sizeof b, 17);
	num::FormatPlain(m_ymin, c2, sizeof c2, 17);
	num::FormatPlain(m_ymax, d, sizeof d, 17);
	StrCopy(line, a, sizeof line);
	StrAppend(line, ",", sizeof line);
	StrAppend(line, b, sizeof line);
	StrAppend(line, ",", sizeof line);
	StrAppend(line, c2, sizeof line);
	StrAppend(line, ",", sizeof line);
	StrAppend(line, d, sizeof line);
	AppendLine(buf, sizeof buf, len, "window=", line);
	num::FormatPlain(m_tblStart, a, sizeof a, 17);
	num::FormatPlain(m_tblStep, b, sizeof b, 17);
	StrCopy(line, a, sizeof line);
	StrAppend(line, ",", sizeof line);
	StrAppend(line, b, sizeof line);
	AppendLine(buf, sizeof buf, len, "table=", line);
	for (int i = 0; i < expr::kFunctions; i++) {
		char key[8] = "y1=";
		key[1] = (char)('1' + i);
		AppendLine(buf, sizeof buf, len, key, m_fn[i].on ? "on|" : "off|", m_fn[i].edit.text);
	}
	for (int i = 0; i < 26; i++) {
		if (m_env.vars[i] == 0)
			continue;
		char key[8] = "var.a=";
		key[4] = (char)('a' + i);
		num::FormatPlain(m_env.vars[i], a, sizeof a, 17);
		AppendLine(buf, sizeof buf, len, key, a);
	}
	num::FormatPlain(m_env.ans, a, sizeof a, 17);
	AppendLine(buf, sizeof buf, len, "ans=", a);
	for (int i = Max(0, m_histCount - 20); i < m_histCount; i++) {
		StrCopy(line, m_hist[i].in, sizeof line);
		StrAppend(line, "\t", sizeof line);
		StrAppend(line, m_hist[i].out, sizeof line);
		StrAppend(line, m_hist[i].error ? "\t1" : "\t0", sizeof line);
		AppendLine(buf, sizeof buf, len, "hist=", line);
	}
	m_platform->SaveFile(kStateFile, buf, len);
}

static bool ParseSigned(const char *s, double *v)
{
	bool neg = *s == '-';
	if (neg)
		s++;
	if (StrEq(s, "inf") || StrEq(s, "nan"))
		return false;
	int n = num::Parse(s, v);
	if (!n)
		return false;
	if (neg)
		*v = -*v;
	return true;
}

// Splits s at commas into up to max numbers; returns how many were read.
static int ParseList(const char *s, double *out, int max)
{
	int n = 0;
	char part[48];
	while (*s && n < max) {
		int k = 0;
		while (*s && *s != ',' && k < (int)sizeof part - 1)
			part[k++] = *s++;
		part[k] = 0;
		if (*s == ',')
			s++;
		if (!ParseSigned(part, &out[n]))
			return n;
		n++;
	}
	return n;
}

void App::Load()
{
	static char buf[16384];
	int len = 0;
	if (!m_platform->LoadFile(kStateFile, buf, sizeof buf - 1, &len))
		return;
	buf[len] = 0;
	char *p = buf;
	while (*p) {
		char *line = p;
		while (*p && *p != '\n')
			p++;
		if (*p)
			*p++ = 0;
		int ll = StrLen(line);
		if (ll && line[ll - 1] == '\r')
			line[ll - 1] = 0;
		char *eq = line;
		while (*eq && *eq != '=')
			eq++;
		if (!*eq || line[0] == '#')
			continue;
		*eq = 0;
		const char *key = line, *val = eq + 1;
		double v[4];
		if (StrEq(key, "angle"))
			m_env.degrees = StrEq(val, "deg");
		else if (StrEq(key, "window") && ParseList(val, v, 4) == 4 && v[0] < v[1] && v[2] < v[3])
			SetView(v[0], v[1], v[2], v[3]);
		else if (StrEq(key, "table") && ParseList(val, v, 2) == 2 && v[1] != 0)
			m_tblStart = v[0], m_tblStep = v[1];
		else if (key[0] == 'y' && key[1] >= '1' && key[1] < '1' + expr::kFunctions && !key[2]) {
			Fn &f = m_fn[key[1] - '1'];
			f.on = val[0] != 'o' || val[1] != 'f';
			while (*val && *val != '|')
				val++;
			if (*val == '|')
				val++;
			f.edit.Set(val);
			f.edit.Home();
			Recompile(key[1] - '1');
		} else if (key[0] == 'v' && key[3] == '.' && key[4] >= 'a' && key[4] <= 'z' && !key[5]) {
			if (ParseSigned(val, &v[0]))
				m_env.vars[key[4] - 'a'] = v[0];
		} else if (StrEq(key, "ans")) {
			if (ParseSigned(val, &v[0]))
				m_env.ans = v[0];
		} else if (StrEq(key, "hist") && m_histCount < kHist) {
			HistEntry &h = m_hist[m_histCount];
			const char *t1 = val;
			while (*t1 && *t1 != '\t')
				t1++;
			if (!*t1)
				continue;
			StrCopy(h.in, val, Min((int)sizeof h.in, (int)(t1 - val) + 1));
			const char *t2 = t1 + 1;
			while (*t2 && *t2 != '\t')
				t2++;
			StrCopy(h.out, t1 + 1, Min((int)sizeof h.out, (int)(t2 - t1)));
			h.error = *t2 == '\t' && t2[1] == '1';
			m_histCount++;
		}
	}
}

// ---------------------------------------------------------------------------
// Keyboard

char App::KeyChar(const KeyEvent &e)
{
	bool shift = (e.mods & ModShift) != 0;
	if (e.mods & (ModCtrl | ModAlt))
		return 0;
	u8 u = e.usage;
	if (u >= KeyA && u <= KeyA + 25)
		return (char)((shift ? 'A' : 'a') + (u - KeyA));
	if (u >= Key1 && u <= Key0) {
		static const char normal[] = "1234567890", shifted[] = "!@#$%^&*()";
		return (shift ? shifted : normal)[u - Key1];
	}
	if (u >= KeyPad1 && u <= KeyPad0)
		return "1234567890"[u - KeyPad1];
	switch (u) {
	case KeySpace: return ' ';
	case KeyMinus: return shift ? '_' : '-';
	case KeyEqual: return shift ? '+' : '=';
	case KeyLeftBracket: return shift ? '{' : '[';
	case KeyRightBracket: return shift ? '}' : ']';
	case KeyBackslash: return shift ? '|' : '\\';
	case KeySemicolon: return shift ? ':' : ';';
	case KeyApostrophe: return shift ? '"' : '\'';
	case KeyGrave: return shift ? '~' : '`';
	case KeyComma: return shift ? '<' : ',';
	case KeyPeriod: return shift ? '>' : '.';
	case KeySlash: return shift ? '?' : '/';
	case KeyPadSlash: return '/';
	case KeyPadStar: return '*';
	case KeyPadMinus: return '-';
	case KeyPadPlus: return '+';
	case KeyPadDot: return '.';
	}
	return 0;
}

bool App::EditKey(LineEdit &ed, const KeyEvent &e, char ch)
{
	switch (e.usage) {
	case KeyLeft: ed.Left(); return true;
	case KeyRight: ed.Right(); return true;
	case KeyHome: ed.Home(); return true;
	case KeyEnd: ed.End(); return true;
	case KeyBackspace: ed.Backspace(); return true;
	case KeyDelete: ed.Delete(); return true;
	}
	// [ ] and { } are brackets too.
	if (ch == '[' || ch == '{')
		ch = '(';
	else if (ch == ']' || ch == '}')
		ch = ')';
	if (ch >= ' ' && ch < 0x7F && ch != '\\' && ch != '`' && ch != '~' && ch != '"' && ch != '\'' &&
	    ch != ';') {
		char s[2] = {ch, 0};
		ed.Insert(s);
		return true;
	}
	return false;
}

void App::OnKey(const KeyEvent &e)
{
	m_dirty = true;
	m_blinkStart = m_now;
	if (m_leaving >= 0)
		return;
	if (e.usage == KeyF12) {
		m_diag = !m_diag;
		m_diagScroll = 0;
		return;
	}
	if (m_diag) {
		switch (e.usage) {
		case KeyUp: m_diagScroll++; break;
		case KeyDown: m_diagScroll = Max(0, m_diagScroll - 1); break;
		case KeyPageUp: m_diagScroll += 20; break;
		case KeyPageDown: m_diagScroll = Max(0, m_diagScroll - 20); break;
		case KeyHome: m_diagScroll = 1 << 20; break;
		case KeyEnd: m_diagScroll = 0; break;
		case KeyEscape: m_diag = false; break;
		}
		return;
	}
	if (m_menuConfirm) {
		if (e.usage == KeyEnter || e.usage == KeyPadEnter || e.usage == KeyLetter('y')) {
			m_leaving = 0;
			Save();
		}
		m_menuConfirm = false;
		return;
	}
	if (e.usage == KeyF(10) && m_platform->HasMenu()) {
		m_menuConfirm = true;
		return;
	}
	if (e.usage >= KeyF1 && e.usage < KeyF1 + ScrCount) {
		Go((Screen)(e.usage - KeyF1));
		return;
	}
	if (e.usage == KeyF(7)) {
		m_env.degrees = !m_env.degrees;
		m_plotValid = false;
		if (m_screen == ScrSettings)
			LoadSettingsText();
		Changed();
		return;
	}
	char ch = KeyChar(e);
	switch (m_screen) {
	case ScrCalc: KeyCalc(e, ch); break;
	case ScrFuncs: KeyFuncs(e, ch); break;
	case ScrGraph: KeyGraph(e, ch); break;
	case ScrTable: KeyTable(e, ch); break;
	case ScrSettings: KeySettings(e, ch); break;
	case ScrHelp:
		if (e.usage == KeyEscape)
			Go(ScrCalc);
		break;
	case ScrCount: break;
	}
}

void App::Update()
{
	m_now = m_platform->Millis();
	int phase = CursorOn() ? 1 : 0;
	if (phase != m_blinkPhase) {
		m_blinkPhase = phase;
		m_dirty = true;
	}
	if (m_leaving >= 0 || m_menuConfirm || m_diag)
		m_dirty = true;
	if (m_leaving >= 2 && m_leaving < kLeft) {
		m_leaving = kLeft;
		m_platform->ReturnToMenu();
	}
	// Save a few seconds after the last change (kids pull the power).
	if (m_saveDirty && m_now - m_lastChange > 3000)
		Save();
}

// ---------------------------------------------------------------------------
// Calculate

static bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool IsSpace(char c) { return c == ' ' || c == '\t'; }

void App::KeyCalc(const KeyEvent &e, char ch)
{
	switch (e.usage) {
	case KeyEnter:
	case KeyPadEnter:
		Evaluate();
		return;
	case KeyEscape:
		m_input.Clear();
		m_recall = -1;
		return;
	case KeyUp:
		if (m_histCount) {
			m_recall = m_recall < 0 ? m_histCount - 1 : Max(0, m_recall - 1);
			m_input.Set(m_hist[m_recall].in);
		}
		return;
	case KeyDown:
		if (m_recall >= 0) {
			m_recall++;
			if (m_recall >= m_histCount) {
				m_recall = -1;
				m_input.Clear();
			} else
				m_input.Set(m_hist[m_recall].in);
		}
		return;
	}
	// An operator on an empty line continues from the last answer.
	if (m_input.len == 0 && m_histCount && (ch == '+' || ch == '*' || ch == '/' || ch == '^' || ch == '!'))
		m_input.Insert("ans");
	EditKey(m_input, e, ch);
}

void App::Evaluate()
{
	if (m_input.Empty())
		return;
	m_recall = -1;
	if (m_histCount == kHist) {
		for (int i = 1; i < kHist; i++)
			m_hist[i - 1] = m_hist[i];
		m_histCount--;
	}
	HistEntry &h = m_hist[m_histCount++];
	StrCopy(h.in, m_input.text, sizeof h.in);
	h.error = false;
	const char *s = m_input.text;
	while (IsSpace(*s))
		s++;

	// "y1 = x^2" defines a function.
	if ((s[0] == 'y' || s[0] == 'Y') && s[1] >= '1' && s[1] < '1' + expr::kFunctions) {
		const char *q = s + 2;
		while (IsSpace(*q))
			q++;
		if (*q == '=') {
			int i = s[1] - '1';
			q++;
			while (IsSpace(*q))
				q++;
			expr::Program test;
			expr::Error err;
			if (!*q) {
				m_fn[i].edit.Clear();
				Recompile(i);
				StrCopy(h.out, "Cleared y1", sizeof h.out);
				h.out[9] = s[1];
			} else if (!expr::Compile(q, &test, &err)) {
				StrCopy(h.out, err.msg, sizeof h.out);
				h.error = true;
			} else {
				m_fn[i].edit.Set(q);
				m_fn[i].on = true;
				Recompile(i);
				StrCopy(h.out, "Saved as y1 (F3 to graph)", sizeof h.out);
				h.out[10] = s[1];
			}
			m_input.Clear();
			Changed();
			return;
		}
	}

	// "a = 5" stores a value in a letter.
	int target = -1;
	if (IsAlpha(s[0])) {
		const char *q = s + 1;
		while (IsSpace(*q))
			q++;
		if (*q == '=') {
			char v = s[0] >= 'A' && s[0] <= 'Z' ? (char)(s[0] + 32) : s[0];
			if (v == 'e') {
				StrCopy(h.out, "e is the number 2.718..., use another letter", sizeof h.out);
				h.error = true;
				m_input.Clear();
				return;
			}
			target = v - 'a';
			s = q + 1;
		}
	}
	expr::Program p;
	expr::Error err;
	if (!expr::Compile(s, &p, &err)) {
		StrCopy(h.out, err.msg, sizeof h.out);
		h.error = true;
		// Keep the text so it can be fixed; put the cursor where the problem is.
		if (err.pos >= 0)
			m_input.cur = Clamp((int)(s - m_input.text) + err.pos, 0, m_input.len);
		Changed();
		return;
	}
	double v = expr::Eval(p, m_env, m_env.vars['x' - 'a']);
	char val[64];
	num::Format(v, val, sizeof val);
	if (target >= 0) {
		if (m::IsFinite(v)) {
			m_env.vars[target] = v;
			char name[2] = {(char)('a' + target), 0};
			StrCopy(h.out, name, sizeof h.out);
			StrAppend(h.out, " = ", sizeof h.out);
			StrAppend(h.out, val, sizeof h.out);
			m_plotValid = false;  // functions may use the letter
		} else {
			StrCopy(h.out, "That has no value, so it was not stored", sizeof h.out);
			h.error = true;
		}
	} else {
		StrCopy(h.out, val, sizeof h.out);
		if (m::IsFinite(v))
			m_env.ans = v;
	}
	m_input.Clear();
	Changed();
}

// ---------------------------------------------------------------------------
// Functions

void App::KeyFuncs(const KeyEvent &e, char ch)
{
	Fn &f = m_fn[m_fnSel];
	switch (e.usage) {
	case KeyUp:
		m_fnSel = (m_fnSel + expr::kFunctions - 1) % expr::kFunctions;
		m_fn[m_fnSel].edit.End();
		return;
	case KeyDown:
	case KeyEnter:
	case KeyPadEnter:
		m_fnSel = (m_fnSel + 1) % expr::kFunctions;
		m_fn[m_fnSel].edit.End();
		return;
	case KeyTab:
		f.on = !f.on;
		m_plotValid = false;
		if (m_traceFn == m_fnSel && !f.on)
			m_traceFn = -1;
		Changed();
		return;
	case KeyEscape:
		f.edit.Clear();
		Recompile(m_fnSel);
		Changed();
		return;
	}
	if (EditKey(f.edit, e, ch)) {
		if (e.usage != KeyLeft && e.usage != KeyRight && e.usage != KeyHome && e.usage != KeyEnd) {
			Recompile(m_fnSel);
			Changed();
		}
	}
}

// ---------------------------------------------------------------------------
// Settings

static void SetNumber(LineEdit &ed, double v)
{
	char s[40];
	num::FormatPlain(v, s, sizeof s, 10);
	ed.Set(s);
}

void App::LoadSettingsText()
{
	SetNumber(m_setEdit[SetXMin], m_xmin);
	SetNumber(m_setEdit[SetXMax], m_xmax);
	SetNumber(m_setEdit[SetYMin], m_ymin);
	SetNumber(m_setEdit[SetYMax], m_ymax);
	SetNumber(m_setEdit[SetTblStart], m_tblStart);
	SetNumber(m_setEdit[SetTblStep], m_tblStep);
}

void App::ApplySetting(int row)
{
	if (row > SetTblStep)
		return;
	bool ok;
	char err[80];
	double v = EvalText(m_setEdit[row].text, &ok, err, sizeof err);
	if (!ok) {
		StrCopy(m_msg, err, sizeof m_msg);
		m_msgError = true;
		LoadSettingsText();
		return;
	}
	double xmin = m_xmin, xmax = m_xmax, ymin = m_ymin, ymax = m_ymax;
	switch (row) {
	case SetXMin: xmin = v; break;
	case SetXMax: xmax = v; break;
	case SetYMin: ymin = v; break;
	case SetYMax: ymax = v; break;
	case SetTblStart: m_tblStart = v; m_tblSel = m_tblTop = 0; break;
	case SetTblStep:
		if (v == 0) {
			StrCopy(m_msg, "The step can't be 0", sizeof m_msg);
			m_msgError = true;
			LoadSettingsText();
			return;
		}
		m_tblStep = v;
		break;
	}
	if (row <= SetYMax) {
		if (xmin >= xmax || ymin >= ymax) {
			StrCopy(m_msg, "The smallest value must be less than the biggest", sizeof m_msg);
			m_msgError = true;
			LoadSettingsText();
			return;
		}
		SetView(xmin, xmax, ymin, ymax);
	}
	m_msg[0] = 0;
	LoadSettingsText();
	Changed();
}

void App::KeySettings(const KeyEvent &e, char ch)
{
	switch (e.usage) {
	case KeyUp:
	case KeyDown: {
		if (m_setSel <= SetTblStep)
			ApplySetting(m_setSel);
		int d = e.usage == KeyUp ? SetRows - 1 : 1;
		m_setSel = (m_setSel + d) % SetRows;
		if (m_setSel <= SetTblStep)
			m_setEdit[m_setSel].End();
		return;
	}
	case KeyEnter:
	case KeyPadEnter:
		if (m_setSel <= SetTblStep) {
			ApplySetting(m_setSel);
			if (!m_msg[0]) {
				m_setSel = (m_setSel + 1) % SetRows;
				if (m_setSel <= SetTblStep)
					m_setEdit[m_setSel].End();
			}
		} else if (m_setSel == SetAngle) {
			m_env.degrees = !m_env.degrees;
			m_plotValid = false;
			Changed();
		} else {
			StandardView();
			m_tblStart = 0;
			m_tblStep = 1;
			m_tblSel = m_tblTop = 0;
			LoadSettingsText();
			StrCopy(m_msg, "Back to the standard window", sizeof m_msg);
			m_msgError = false;
			Changed();
		}
		return;
	case KeyEscape:
		LoadSettingsText();
		m_msg[0] = 0;
		return;
	}
	if (m_setSel == SetAngle) {
		if (e.usage == KeyLeft || e.usage == KeyRight || e.usage == KeySpace) {
			m_env.degrees = !m_env.degrees;
			m_plotValid = false;
			Changed();
		}
		return;
	}
	if (m_setSel <= SetTblStep)
		EditKey(m_setEdit[m_setSel], e, ch);
}

// ---------------------------------------------------------------------------
// Drawing: shared parts

void App::DrawTabs(Canvas &c)
{
	c.FillRect(0, 0, W, kTabH, theme::Navy);
	int tw = W / ScrCount;
	for (int i = 0; i < ScrCount; i++) {
		int x = i * tw;
		bool sel = i == m_screen;
		if (sel) {  // a tab joined to the page below (top corners rounded)
			c.SetClip(0, 0, W, kTabH);
			c.FillRoundRect(x + 6, 8, tw - 12, kTabH + 8, 14, m_screen == ScrGraph ? theme::Panel : theme::Bg);
			c.ResetClip();
		}
		char key[4] = {'F', (char)('1' + i), 0};
		Color fg = sel ? theme::Navy : 0xFFE8ECF8;
		int labelW = Canvas::Measure(key, font::Small) + 8 + Canvas::Measure(kTabNames[i], font::Body);
		int lx = x + (tw - labelW) / 2;
		c.FillRoundRect(lx - 4, 18, Canvas::Measure(key, font::Small) + 8, 28, 6,
				sel ? theme::Accent : 0x40FFFFFF);
		c.Text(key, font::Small, lx, 40, sel ? 0xFFFFFFFF : 0xFFE8ECF8);
		c.Text(kTabNames[i], font::Body, lx + Canvas::Measure(key, font::Small) + 12, 42, fg);
	}
}

void App::DrawFooter(Canvas &c, const char *hint)
{
	c.FillRect(0, H - 44, W, 44, theme::Footer);
	if (m_platform->StorageAvailable())
		c.Text(hint, font::Small, 24, H - 15, theme::InkSoft);
	else
		c.Text("The SD card can't be used: your work will not be kept.", font::Small, 24, H - 15, theme::Bad);
	// Angle unit and the way back to the menu, on the right.
	char right[64] = "";
	if (m_platform->HasMenu())
		StrAppend(right, "F10 menu    ", sizeof right);
	StrAppend(right, "F7 ", sizeof right);
	StrAppend(right, m_env.degrees ? "DEG" : "RAD", sizeof right);
	c.Text(right, font::Small, W - 24, H - 15, theme::InkSoft, AlignRight);
}

// Draws the editable text with a cursor; scrolls long text so the cursor shows.
void App::DrawEdit(Canvas &c, const LineEdit &ed, font::Size size, int x, int baseline, int maxW, bool focus,
		   Color color)
{
	char before[LineEdit::kCap + 1];
	StrCopy(before, ed.text, ed.cur + 1);
	int curX = Canvas::Measure(before, size);
	int scroll = curX > maxW - 20 ? curX - (maxW - 20) : 0;
	int asc = font::g_Latin[size].ascent;
	c.SetClip(x, baseline - asc - 4, maxW, asc + font::g_Latin[size].descent + 8);
	c.Text(ed.text, size, x - scroll, baseline, color);
	if (focus && CursorOn())
		c.FillRect(x - scroll + curX, baseline - asc * 3 / 4 - 4, 3, asc * 3 / 4 + 10, theme::Accent);
	c.ResetClip();
}

// Shows the text the way it is printed in books: × for *, π for pi, √ for sqrt.
static void Pretty(const char *s, char *out, int cap)
{
	int n = 0;
	auto put = [&](const char *t) {
		while (*t && n < cap - 1)
			out[n++] = *t++;
	};
	for (int i = 0; s[i]; i++) {
		bool wordStart = i == 0 || !IsAlpha(s[i - 1]);
		if (s[i] == '*')
			put("\xC3\x97");
		else if (wordStart && (s[i] == 'p' || s[i] == 'P') && (s[i + 1] == 'i' || s[i + 1] == 'I') &&
			 !IsAlpha(s[i + 2])) {
			put("\xCF\x80");
			i++;
		} else if (wordStart && s[i] == 's' && s[i + 1] == 'q' && s[i + 2] == 'r' && s[i + 3] == 't' &&
			   s[i + 4] == '(') {
			put("\xE2\x88\x9A");
			i += 3;
		} else {
			char t[2] = {s[i], 0};
			put(t);
		}
	}
	out[n] = 0;
}

void App::DrawCalc(Canvas &c)
{
	// History, newest at the bottom, above the input line.
	int inputTop = H - 44 - 96;
	int y = inputTop - 14;
	c.SetClip(0, kTabH, W, inputTop - kTabH);
	for (int i = m_histCount - 1; i >= 0 && y > kTabH; i--) {
		const HistEntry &h = m_hist[i];
		char pretty[200];
		Pretty(h.in, pretty, sizeof pretty);
		bool recalled = i == m_recall;
		if (recalled)
			c.FillRoundRect(16, y - 96, W - 32, 92, 12, theme::Highlight);
		c.Text(pretty, font::Body, 40, y - 58, theme::InkSoft);
		c.Text(h.out, h.error ? font::Body : font::Title, W - 40, y - 12, h.error ? theme::Bad : theme::Ink,
		       AlignRight);
		c.FillRect(40, y, W - 80, 1, theme::Line);
		y -= 100;
	}
	c.ResetClip();
	if (!m_histCount) {
		c.Text("Type a calculation and press Enter", font::Title, W / 2, 250, theme::InkSoft, AlignCenter);
		c.Text("2+3*4      sqrt(2)      sin(30)      5!      a = 7      y1 = x^2 - 4", font::Body, W / 2,
		       320, theme::Faint, AlignCenter);
	}
	// Input line.
	c.FillRoundRect(16, inputTop, W - 32, 82, 16, theme::Panel);
	c.StrokeRoundRect(16, inputTop, W - 32, 82, 16, 3, theme::Accent);
	DrawEdit(c, m_input, font::Title, 40, inputTop + 56, W - 80, true, theme::Ink);
	DrawFooter(c, "Enter work out    \xE2\x86\x91\xE2\x86\x93 earlier lines    Esc clear    F6 help");
}

void App::DrawFuncs(Canvas &c)
{
	int rowH = 92, top = kTabH + 24;
	for (int i = 0; i < expr::kFunctions; i++) {
		const Fn &f = m_fn[i];
		int y = top + i * rowH;
		bool sel = i == m_fnSel;
		c.FillRoundRect(16, y, W - 32, rowH - 12, 14, sel ? theme::Panel : theme::PanelSoft);
		if (sel)
			c.StrokeRoundRect(16, y, W - 32, rowH - 12, 14, 3, theme::Fn[i]);
		// Colour dot: filled when shown on the graph.
		if (f.on)
			c.FillCircle(52, y + 40, 14, theme::Fn[i]);
		else {
			c.FillCircle(52, y + 40, 14, theme::Fn[i]);
			c.FillCircle(52, y + 40, 10, sel ? theme::Panel : theme::PanelSoft);
		}
		char name[8] = "y1 =";
		name[1] = (char)('1' + i);
		c.Text(name, font::Title, 84, y + 56, f.on ? theme::Ink : theme::Faint);
		DrawEdit(c, f.edit, font::Title, 190, y + 56, W - 260, sel, f.on ? theme::Ink : theme::Faint);
		if (!f.edit.Empty() && !f.ok)
			c.Text(f.err.msg, font::Small, W - 48, y + 72, theme::Bad, AlignRight);
	}
	DrawFooter(c, "\xE2\x86\x91\xE2\x86\x93 choose    Tab show/hide    Esc clear    F3 graph");
}

void App::DrawSettings(Canvas &c)
{
	static const char *const labels[SetRows] = {"X smallest", "X biggest",     "Y smallest", "Y biggest",
						     "Table start", "Table step", "Angles in",   ""};
	int rowH = 66, top = kTabH + 22;
	for (int i = 0; i < SetRows; i++) {
		int y = top + i * rowH + (i >= SetTblStart ? 16 : 0) + (i >= SetAngle ? 16 : 0);
		bool sel = i == m_setSel;
		c.FillRoundRect(220, y, 840, rowH - 10, 12, sel ? theme::Panel : theme::PanelSoft);
		if (sel)
			c.StrokeRoundRect(220, y, 840, rowH - 10, 12, 3, theme::Accent);
		if (i == SetStandard) {
			c.Text("Reset to the standard window", font::Body, 640, y + 38, theme::Navy, AlignCenter);
			continue;
		}
		c.Text(labels[i], font::Body, 250, y + 38, theme::InkSoft);
		if (i == SetAngle) {
			c.Text(m_env.degrees ? "\xE2\x86\x90  Degrees  \xE2\x86\x92" : "\xE2\x86\x90  Radians  \xE2\x86\x92",
			       font::Body, 520, y + 38, theme::Ink);
			continue;
		}
		DrawEdit(c, m_setEdit[i], font::Body, 520, y + 38, 500, sel, theme::Ink);
	}
	if (m_msg[0])
		c.Text(m_msg, font::Body, W / 2, H - 64, m_msgError ? theme::Bad : theme::Good, AlignCenter);
	DrawFooter(c, "\xE2\x86\x91\xE2\x86\x93 choose    Enter apply    Esc undo");
}

void App::DrawHelp(Canvas &c)
{
	static const char *const left[][2] = {
		{"Typing maths", ""},
		{"+  -  *  /", "add, subtract, times, divide"},
		{"^", "power: x^2, 2^-1"},
		{"2x, 3(x+1)", "times without *"},
		{"( )  [ ]", "brackets"},
		{"|x - 2|", "distance from 0 (abs)"},
		{"5!", "factorial"},
		{"pi  e  ans", "numbers you can use"},
		{"a = 7", "keep a value in a letter"},
		{"y1 = x^2", "a function to graph"},
		{"1.5E-3", "0.0015 (capital E)"},
	};
	static const char *const right[] = {
		"Functions",
		"sin cos tan  asin acos atan",
		"sqrt  cbrt  root(x, n)",
		"ln  log  log(x, base)  exp",
		"abs  floor  ceil  round(x, places)",
		"ncr(n, r)  npr(n, r)  gcd  lcm",
		"min  max  mod  sign",
		"sinh  cosh  tanh",
		"",
		"Graph keys",
		"\xE2\x86\x90\xE2\x86\x92 trace  \xE2\x86\x91\xE2\x86\x93 function  + - zoom",
		"R zero   N min   M max   I meet",
		"G go to x   F fit   S square",
		"Ctrl+arrows move   Home standard",
	};
	for (int i = 0; i < ARRAY_LEN(left); i++) {
		int y = kTabH + 70 + i * 46 + (i ? 10 : 0);
		c.Text(left[i][0], i ? font::Small : font::Title, 60, y, i ? theme::Navy : theme::Navy);
		c.Text(left[i][1], font::Small, 250, y, theme::Ink);
	}
	for (int i = 0; i < ARRAY_LEN(right); i++) {
		bool head = i == 0 || i == 9;
		c.Text(right[i], head ? font::Title : font::Small, 700, kTabH + 70 + i * 40 + (i ? 10 : 0),
		       head ? theme::Navy : theme::Ink);
	}
	DrawFooter(c, "F1 - F6 switch screens    F7 degrees / radians    F12 system log");
}

void App::DrawDiagnostics(Canvas &c)
{
	c.FillRoundRect(20, 20, W - 40, H - 40, 16, 0xF01E2336);
	static const char *lines[400];
	int n = m_platform->Diagnostics(lines, 400);
	int rows = (H - 120) / 24;
	int scroll = Clamp(m_diagScroll, 0, Max(0, n - rows));
	int first = Max(0, n - rows - scroll);
	c.Text("System log  (arrows / PgUp / PgDn to scroll, Esc or F12 to close)", font::Small, 40, 54,
	       0xFFFFE27A);
	for (int i = first; i < n && i < first + rows; i++)
		c.Text(lines[i], font::Tiny, 40, 84 + (i - first) * 24, 0xFFE8EAF2);
}

void App::DrawMenuConfirm(Canvas &c)
{
	c.FillRect(0, 0, W, H, 0x90202848);
	int pw = 760, ph = m_leaving >= 0 ? 160 : 230, px = (W - pw) / 2, py = (H - ph) / 2;
	c.FillRoundRect(px, py, pw, ph, 30, theme::Panel);
	if (m_leaving >= 0) {
		c.Text("Going back to the menu...", font::Title, W / 2, py + 96, theme::Navy, AlignCenter);
		if (m_leaving < kLeft)
			m_leaving++;
		return;
	}
	c.Text("Go back to the menu?", font::Title, W / 2, py + 90, theme::Navy, AlignCenter);
	c.Text("Your functions and work are kept.", font::Body, W / 2, py + 140, theme::InkSoft, AlignCenter);
	c.Text("Enter: yes      Esc: stay", font::Body, W / 2, py + 196, theme::Ink, AlignCenter);
}

bool App::Draw(Canvas &c)
{
	if (!m_dirty)
		return false;
	m_dirty = false;
	if (m_screen != ScrGraph)
		c.Clear(theme::Bg);
	switch (m_screen) {
	case ScrCalc: DrawCalc(c); break;
	case ScrFuncs: DrawFuncs(c); break;
	case ScrGraph: DrawGraph(c); break;
	case ScrTable: DrawTable(c); break;
	case ScrSettings: DrawSettings(c); break;
	case ScrHelp: DrawHelp(c); break;
	case ScrCount: break;
	}
	DrawTabs(c);
	if (m_menuConfirm || m_leaving >= 0)
		DrawMenuConfirm(c);
	if (m_diag)
		DrawDiagnostics(c);
	return true;
}
