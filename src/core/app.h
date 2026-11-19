// The graphing calculator: calculate, enter functions, graph them, trace
// and analyse them, tables of values. Platform independent; the Pi kernel and
// the desktop simulator both drive it.
#pragma once
#include "base.h"
#include "expr.h"
#include "gfx.h"
#include "keys.h"
#include "lineedit.h"
#include "platform.h"

enum Screen { ScrCalc, ScrFuncs, ScrGraph, ScrTable, ScrSettings, ScrHelp, ScrCount };

class App
{
public:
	static const int W = 1280, H = 720;
	static const int kTabH = 60;                         // screen tabs at the top
	static const int kStatusH = 84;                      // graph status bar at the bottom
	static const int kPlotY = kTabH, kPlotH = H - kTabH - kStatusH;
	static const int kHist = 40;

	explicit App(Platform *platform);
	void Init();
	void OnKey(const KeyEvent &e);
	void Update();
	// Renders a frame if anything changed; returns false when nothing was drawn.
	bool Draw(Canvas &c);

	// Introspection for the simulator / tests.
	Screen CurrentScreen() const { return m_screen; }
	const char *LastResult() const { return m_histCount ? m_hist[m_histCount - 1].out : ""; }
	const char *StatusText() const;  // graph/table/settings message
	static char KeyChar(const KeyEvent &e);  // US layout; 0 for non-printing keys

private:
	struct Fn
	{
		LineEdit edit;
		bool on;
		bool ok;  // compiled (an empty line is not ok, but not an error either)
		expr::Program prog;
		expr::Error err;
	};
	struct HistEntry
	{
		char in[LineEdit::kCap + 1];
		char out[64];
		bool error;
	};
	enum SetRow { SetXMin, SetXMax, SetYMin, SetYMax, SetTblStart, SetTblStep, SetAngle, SetStandard, SetRows };

	// --- flow and state
	void Go(Screen s);
	void Changed() { m_saveDirty = true, m_lastChange = m_now; }
	void Save();
	void Load();
	void Recompile(int i);
	double EvalText(const char *text, bool *ok, char *err, int errCap);  // for settings / go to x
	bool Visible(int fn) const { return m_fn[fn].on && m_fn[fn].ok; }

	// --- per screen
	void KeyCalc(const KeyEvent &e, char ch);
	void KeyFuncs(const KeyEvent &e, char ch);
	void KeyGraph(const KeyEvent &e, char ch);
	void KeyTable(const KeyEvent &e, char ch);
	void KeySettings(const KeyEvent &e, char ch);
	bool EditKey(LineEdit &ed, const KeyEvent &e, char ch);  // editing keys shared by all fields
	void Evaluate();
	void ApplySetting(int row);
	void LoadSettingsText();

	void DrawTabs(Canvas &c);
	void DrawFooter(Canvas &c, const char *hint);
	void DrawEdit(Canvas &c, const LineEdit &ed, font::Size size, int x, int baseline, int maxW, bool focus,
		      Color color);
	void DrawCalc(Canvas &c);
	void DrawFuncs(Canvas &c);
	void DrawGraph(Canvas &c);
	void DrawTable(Canvas &c);
	void DrawSettings(Canvas &c);
	void DrawHelp(Canvas &c);
	void DrawDiagnostics(Canvas &c);
	void DrawMenuConfirm(Canvas &c);
	bool CursorOn() const { return ((m_now - m_blinkStart) / 530) % 2 == 0; }

	// --- graph (graph.cpp)
	double ToPx(double x) const { return (x - m_xmin) * W / (m_xmax - m_xmin); }
	double ToPy(double y) const { return (m_ymax - y) * kPlotH / (m_ymax - m_ymin); }
	double FromPx(double px) const { return m_xmin + px * (m_xmax - m_xmin) / W; }
	double PixelW() const { return (m_xmax - m_xmin) / W; }
	double F(int fn, double x) const { return expr::Eval(m_fn[fn].prog, m_env, x); }
	void StandardView();
	void SetView(double xmin, double xmax, double ymin, double ymax);
	void ZoomAt(double cx, double cy, double factor);
	void ZoomFit();
	void SquareView();
	void RenderPlot();
	void PlotFunction(int fn);
	void PlotSegment(int fn, double x0, double y0, double x1, double y1, int depth);
	void CoverLine(float x0, float y0, float x1, float y1);
	void FirstTrace();
	void NextTrace(int dir);
	void MoveTrace(double x);
	void Analyse(char what);  // 'r' root, 'n' minimum, 'm' maximum, 'i' intersection
	bool FindRoot(int f, int g, double *x);
	bool FindExtremum(int f, bool max, double *x);
	void DrawAxes(Canvas &c);
	static double NiceStep(double range, int targetLines);

	Platform *m_platform;
	Screen m_screen;
	u32 m_now, m_blinkStart, m_lastChange;
	bool m_dirty, m_saveDirty;
	int m_blinkPhase;

	expr::Env m_env;
	Fn m_fn[expr::kFunctions];

	// Calculate
	LineEdit m_input;
	HistEntry m_hist[kHist];
	int m_histCount;
	int m_recall;  // history entry recalled with Up/Down, or -1

	// Functions
	int m_fnSel;

	// Graph
	double m_xmin, m_xmax, m_ymin, m_ymax;
	int m_traceFn;  // -1: none
	double m_traceX;
	bool m_mark;
	double m_markX, m_markY;
	char m_msg[128];
	bool m_msgError;
	bool m_gotoOpen;
	LineEdit m_gotoEdit;
	bool m_plotValid;
	u32 *m_plot;  // cached graph picture (W x kPlotH)
	u8 *m_cover;  // coverage of the curve being drawn

	// Table
	double m_tblStart, m_tblStep;
	int m_tblSel;  // selected row (0 = the start value; can go negative)
	int m_tblTop;  // first row shown

	// Settings
	int m_setSel;
	LineEdit m_setEdit[SetTblStep + 1];

	// Back to the boot menu
	bool m_menuConfirm;
	int m_leaving;  // frames since "Going back to the menu", -1 when staying

	// F12 log viewer
	bool m_diag;
	int m_diagScroll;
};
