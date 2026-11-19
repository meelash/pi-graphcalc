// Graph and table screens: drawing functions, tracing and finding zeros,
// minimums, maximums and where two graphs meet.
#include "app.h"
#include "mathlib.h"
#include "numfmt.h"
#include "theme.h"

static const int kMaxPlotDepth = 10;  // halvings of one pixel column on steep parts
static const float kCurveWidth = 3.2f;

// ---------------------------------------------------------------------------
// View

void App::StandardView()
{
	// x from -10 to 10, with y scaled the same so circles look round.
	double half = 10.0 * kPlotH / W;
	SetView(-10, 10, -half, half);
}

void App::SetView(double xmin, double xmax, double ymin, double ymax)
{
	// Keep the window in a range where pixels still have distinct x values.
	double cx = (xmin + xmax) / 2, cy = (ymin + ymax) / 2;
	double hw = (xmax - xmin) / 2, hh = (ymax - ymin) / 2;
	double minHalf = 1e-9 * Max(1.0, m::Abs(cx)), minHalfY = 1e-9 * Max(1.0, m::Abs(cy));
	hw = Clamp(hw, minHalf, 1e12);
	hh = Clamp(hh, minHalfY, 1e12);
	m_xmin = cx - hw, m_xmax = cx + hw;
	m_ymin = cy - hh, m_ymax = cy + hh;
	m_plotValid = false;
}

void App::ZoomAt(double cx, double cy, double factor)
{
	double hw = (m_xmax - m_xmin) / 2 * factor, hh = (m_ymax - m_ymin) / 2 * factor;
	SetView(cx - hw, cx + hw, cy - hh, cy + hh);
}

void App::SquareView()
{
	double cy = (m_ymin + m_ymax) / 2;
	double hh = (m_xmax - m_xmin) / 2 * kPlotH / W;
	SetView(m_xmin, m_xmax, cy - hh, cy + hh);
}

void App::ZoomFit()
{
	double lo = m::Inf(), hi = -m::Inf();
	for (int f = 0; f < expr::kFunctions; f++) {
		if (!Visible(f))
			continue;
		for (int px = 0; px <= W; px += 2) {
			double y = F(f, FromPx(px));
			if (!m::IsFinite(y) || m::Abs(y) > 1e9)
				continue;
			lo = Min(lo, y), hi = Max(hi, y);
		}
	}
	if (lo > hi) {
		StrCopy(m_msg, "Nothing to fit: no graph has values here", sizeof m_msg);
		m_msgError = true;
		return;
	}
	if (hi - lo < 1e-9 * Max(1.0, m::Abs(lo)))
		lo -= 1, hi += 1;
	double pad = (hi - lo) * 0.08;
	SetView(m_xmin, m_xmax, lo - pad, hi + pad);
}

double App::NiceStep(double range, int targetLines)
{
	double raw = range / Max(1, targetLines);
	double mag = num::Pow10((int)m::Floor(m::Log10(raw)));
	double norm = raw / mag;
	double step = norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10;
	return step * mag;
}

// ---------------------------------------------------------------------------
// Drawing the graph into m_plot (only when something changed)

void App::CoverLine(float x0, float y0, float x1, float y1)
{
	float r = kCurveWidth * 0.5f;
	int bx0 = Max(0, (int)(Min(x0, x1) - r - 1)), bx1 = Min(W, (int)(Max(x0, x1) + r + 2));
	int by0 = Max(0, (int)(Min(y0, y1) - r - 1)), by1 = Min(kPlotH, (int)(Max(y0, y1) + r + 2));
	float dx = x1 - x0, dy = y1 - y0;
	float len2 = dx * dx + dy * dy;
	for (int py = by0; py < by1; py++) {
		u8 *row = &m_cover[py * W];
		for (int px = bx0; px < bx1; px++) {
			float qx = px + 0.5f - x0, qy = py + 0.5f - y0;
			float t = len2 > 0 ? Clamp((qx * dx + qy * dy) / len2, 0.0f, 1.0f) : 0;
			float ex = qx - t * dx, ey = qy - t * dy;
			float d2 = ex * ex + ey * ey;
			if (d2 >= (r + 0.5f) * (r + 0.5f))
				continue;
			float cov = r + 0.5f - __builtin_sqrtf(d2);
			u8 a = cov >= 1 ? 255 : (u8)(cov * 255);
			if (a > row[px])
				row[px] = a;  // max, not sum: no dark spots where segments join
		}
	}
}

void App::PlotSegment(int fn, double x0, double y0, double x1, double y1, int depth)
{
	bool f0 = m::IsFinite(y0), f1 = m::IsFinite(y1);
	if (!f0 && !f1)
		return;
	if (f0 != f1) {
		// The edge of where the function exists (sqrt(x) at 0): close in on it.
		if (depth >= kMaxPlotDepth)
			return;
		double xm = (x0 + x1) / 2, ym = F(fn, xm);
		PlotSegment(fn, x0, y0, xm, ym, depth + 1);
		PlotSegment(fn, xm, ym, x1, y1, depth + 1);
		return;
	}
	double py0 = ToPy(y0), py1 = ToPy(y1);
	if ((py0 < -4 && py1 < -4) || (py0 > kPlotH + 4 && py1 > kPlotH + 4))
		return;  // off the screen
	double dy = m::Abs(py1 - py0);
	if (dy > 2 && depth < kMaxPlotDepth) {
		// Steep: sample in between for a smooth curve.
		double xm = (x0 + x1) / 2, ym = F(fn, xm);
		PlotSegment(fn, x0, y0, xm, ym, depth + 1);
		PlotSegment(fn, xm, ym, x1, y1, depth + 1);
		return;
	}
	// Still a jump over a sliver of a pixel: a break in the graph (1/x at 0,
	// tan at 90 degrees, floor) rather than a steep line, so don't join it.
	if (dy > 8)
		return;
	py0 = Clamp(py0, -20.0, kPlotH + 20.0);
	py1 = Clamp(py1, -20.0, kPlotH + 20.0);
	CoverLine((float)ToPx(x0), (float)py0, (float)ToPx(x1), (float)py1);
}

void App::PlotFunction(int fn)
{
	MemZero(m_cover, W * kPlotH);
	double xPrev = FromPx(0), yPrev = F(fn, xPrev);
	for (int px = 1; px <= W; px++) {
		double x = FromPx(px), y = F(fn, x);
		PlotSegment(fn, xPrev, yPrev, x, y, 0);
		xPrev = x, yPrev = y;
	}
	Canvas pc(m_plot, W, kPlotH);
	for (int py = 0; py < kPlotH; py++) {
		const u8 *row = &m_cover[py * W];
		for (int px = 0; px < W; px++)
			if (row[px])
				pc.BlendPixel(px, py, theme::Fn[fn], row[px]);
	}
}

static void Label(double v, char *out, int cap)
{
	num::Format(v, out, cap, 6);
}

void App::DrawAxes(Canvas &pc)
{
	double xr = m_xmax - m_xmin, yr = m_ymax - m_ymin;
	double sx = NiceStep(xr, W / 110), sy = NiceStep(yr, kPlotH / 90);
	// Minor lines (fifths) when they are not too dense.
	for (int pass = 0; pass < 2; pass++) {
		bool minor = pass == 0;
		double stx = minor ? sx / 5 : sx, sty = minor ? sy / 5 : sy;
		if (minor && stx * W / xr < 10)
			continue;
		Color col = minor ? theme::GridMinor : theme::GridMajor;
		for (double k = m::Ceil(m_xmin / stx); k * stx <= m_xmax; k++) {
			int px = (int)m::Round(ToPx(k * stx));
			pc.FillRect(px, 0, 1, kPlotH, col);
		}
		for (double k = m::Ceil(m_ymin / sty); k * sty <= m_ymax; k++) {
			int py = (int)m::Round(ToPy(k * sty));
			pc.FillRect(0, py, W, 1, col);
		}
	}
	// Axes, or the screen edge when an axis is out of view.
	int ax = (int)m::Round(ToPy(0)), ay = (int)m::Round(ToPx(0));
	bool xAxis = ax >= 0 && ax < kPlotH, yAxis = ay >= 0 && ay < W;
	if (xAxis)
		pc.FillRect(0, ax - 1, W, 2, theme::Axis);
	if (yAxis)
		pc.FillRect(ay - 1, 0, 2, kPlotH, theme::Axis);
	int labelY = xAxis ? Clamp(ax + 22, 22, kPlotH - 6) : kPlotH - 8;
	int labelX = yAxis ? Clamp(ay - 8, 60, W - 4) : 60;
	char s[32];
	for (double k = m::Ceil(m_xmin / sx); k * sx <= m_xmax; k++) {
		double v = k * sx;
		if (k == 0)
			continue;
		int px = (int)m::Round(ToPx(v));
		if (px < 20 || px > W - 20)
			continue;
		Label(v, s, sizeof s);
		if (xAxis)
			pc.FillRect(px, ax - 6, 1, 12, theme::Axis);
		pc.FillRect(px - Canvas::Measure(s, font::Tiny) / 2 - 3, labelY - 17, Canvas::Measure(s, font::Tiny) + 6,
			    22, 0xC0FFFFFF);
		pc.Text(s, font::Tiny, px, labelY, theme::InkSoft, AlignCenter);
	}
	for (double k = m::Ceil(m_ymin / sy); k * sy <= m_ymax; k++) {
		double v = k * sy;
		if (k == 0)
			continue;
		int py = (int)m::Round(ToPy(v));
		if (py < 14 || py > kPlotH - 14)
			continue;
		Label(v, s, sizeof s);
		if (yAxis)
			pc.FillRect(ay - 6, py, 12, 1, theme::Axis);
		int w = Canvas::Measure(s, font::Tiny);
		pc.FillRect(labelX - w - 3, py - 11, w + 6, 22, 0xC0FFFFFF);
		pc.Text(s, font::Tiny, labelX, py + 6, theme::InkSoft, AlignRight);
	}
	if (xAxis && yAxis)
		pc.Text("0", font::Tiny, ay - 8, ax + 22, theme::InkSoft, AlignRight);
}

void App::RenderPlot()
{
	Canvas pc(m_plot, W, kPlotH);
	pc.Clear(theme::Panel);
	DrawAxes(pc);
	for (int f = 0; f < expr::kFunctions; f++)
		if (Visible(f))
			PlotFunction(f);
	m_plotValid = true;
}

// ---------------------------------------------------------------------------
// Trace

// Trace steps: a round number about two pixels wide, so x stays tidy (0.02, not 0.015625).
static double TraceStep(double pixelW)
{
	double s = pixelW * 2;
	double mag = num::Pow10((int)m::Floor(m::Log10(s)));
	double norm = s / mag;
	return (norm < 2 ? 1 : norm < 5 ? 2 : 5) * mag;
}

void App::FirstTrace()
{
	m_traceFn = -1;
	for (int f = 0; f < expr::kFunctions; f++)
		if (Visible(f)) {
			m_traceFn = f;
			break;
		}
	double step = TraceStep(PixelW());
	m_traceX = m::Round((m_xmin + m_xmax) / 2 / step) * step;
}

void App::NextTrace(int dir)
{
	for (int k = 1; k <= expr::kFunctions; k++) {
		int f = ((m_traceFn < 0 ? 0 : m_traceFn) + dir * k + 2 * expr::kFunctions) % expr::kFunctions;
		if (Visible(f)) {
			m_traceFn = f;
			return;
		}
	}
}

void App::MoveTrace(double x)
{
	m_traceX = x;
	double w = m_xmax - m_xmin;
	if (x < m_xmin || x > m_xmax) {
		// Scroll so the cursor is back in view, a quarter from the edge.
		double left = x < m_xmin ? x - w / 4 : x - w * 3 / 4;
		SetView(left, left + w, m_ymin, m_ymax);
	}
	// Follow the curve up and down too.
	if (m_traceFn >= 0) {
		double y = F(m_traceFn, x);
		double h = m_ymax - m_ymin;
		if (m::IsFinite(y) && m::Abs(y) < 1e12 && (y < m_ymin || y > m_ymax))
			SetView(m_xmin, m_xmax, y - h / 2, y + h / 2);
	}
}

// ---------------------------------------------------------------------------
// Analysis

static double g_samples[App::W + 1];

bool App::FindRoot(int f, int g, double *out)
{
	auto h = [&](double x) { return F(f, x) - (g >= 0 ? F(g, x) : 0.0); };
	for (int i = 0; i <= W; i++)
		g_samples[i] = h(FromPx(i));
	double best = 0, bestDist = m::Inf();
	auto consider = [&](double x) {
		double d = m::Abs(x - m_traceX);
		if (d < bestDist)
			best = x, bestDist = d;
	};
	for (int i = 0; i < W; i++) {
		double a = g_samples[i], b = g_samples[i + 1];
		double xa = FromPx(i), xb = FromPx(i + 1);
		if (!m::IsFinite(a) || !m::IsFinite(b))
			continue;
		if (a == 0) {
			consider(xa);
			continue;
		}
		if ((a < 0) != (b < 0) && b != 0) {
			// A sign change; skip poles (1/x), where |h| grows toward the middle.
			double mid = h((xa + xb) / 2);
			if (!m::IsFinite(mid) || m::Abs(mid) > Max(m::Abs(a), m::Abs(b)))
				continue;
			double lo = xa, hi = xb, flo = a;
			for (int it = 0; it < 200; it++) {
				double xm = (lo + hi) / 2;
				if (xm == lo || xm == hi)
					break;
				double fm = h(xm);
				if (fm == 0) {
					lo = hi = xm;
					break;
				}
				if ((fm < 0) == (flo < 0))
					lo = xm, flo = fm;
				else
					hi = xm;
			}
			consider((lo + hi) / 2);
			continue;
		}
		// Touching zero without crossing (x^2 at 0): a small local minimum of |h|.
		if (i > 0 && m::IsFinite(g_samples[i - 1])) {
			double p = m::Abs(g_samples[i - 1]), q = m::Abs(a), r = m::Abs(b);
			double scale = (m_ymax - m_ymin) / kPlotH;
			if (q <= p && q <= r && q < scale * 2 && (a < 0) == (g_samples[i - 1] < 0)) {
				double lo = FromPx(i - 1), hi = xb;
				for (int it = 0; it < 120; it++) {
					double m1 = lo + (hi - lo) * 0.381966, m2 = hi - (hi - lo) * 0.381966;
					if (m::Abs(h(m1)) < m::Abs(h(m2)))
						hi = m2;
					else
						lo = m1;
				}
				double x = (lo + hi) / 2;
				if (m::Abs(h(x)) < 1e-9 * Max(1.0, scale))
					consider(x);
			}
		}
	}
	if (bestDist == m::Inf())
		return false;
	*out = best;
	return true;
}

bool App::FindExtremum(int f, bool max, double *out)
{
	for (int i = 0; i <= W; i++)
		g_samples[i] = F(f, FromPx(i));
	double best = 0, bestDist = m::Inf();
	for (int i = 1; i < W; i++) {
		double a = g_samples[i - 1], b = g_samples[i], c = g_samples[i + 1];
		if (!m::IsFinite(a) || !m::IsFinite(b) || !m::IsFinite(c))
			continue;
		bool peak = max ? (b >= a && b >= c) : (b <= a && b <= c);
		if (!peak || (a == b && b == c))
			continue;
		double lo = FromPx(i - 1), hi = FromPx(i + 1);
		// The slope changes sign at the peak: find where with a central
		// difference (precise to ~1e-12, where comparing values of a flat
		// peak can't do better than ~1e-8).
		double hstep = (m_xmax - m_xmin) * 1e-7;
		auto slope = [&](double x) { return F(f, x + hstep) - F(f, x - hstep); };
		double slo = slope(lo), shi = slope(hi);
		double x;
		bool rising = max ? slo > 0 : slo < 0;
		if (m::IsFinite(slo) && m::IsFinite(shi) && rising && (slo > 0) != (shi > 0)) {
			for (int it = 0; it < 200; it++) {
				double xm = (lo + hi) / 2;
				if (xm == lo || xm == hi)
					break;
				double sm = slope(xm);
				if (!m::IsFinite(sm))
					break;
				if ((sm > 0) == (slo > 0))
					lo = xm;
				else
					hi = xm;
			}
			x = (lo + hi) / 2;
		} else {
			// Corners (|x| at 0) or noise: golden-section search on the values.
			for (int it = 0; it < 120; it++) {
				double m1 = lo + (hi - lo) * 0.381966, m2 = hi - (hi - lo) * 0.381966;
				double f1 = F(f, m1), f2 = F(f, m2);
				if (max ? f1 > f2 : f1 < f2)
					hi = m2;
				else
					lo = m1;
			}
			x = (lo + hi) / 2;
		}
		double d = m::Abs(x - m_traceX);
		if (d < bestDist)
			best = x, bestDist = d;
	}
	if (bestDist == m::Inf())
		return false;
	*out = best;
	return true;
}

// Rounds away the noise of a search: values tiny next to the window are 0.
static void FormatFound(double v, double range, char *out, int cap)
{
	if (m::Abs(v) < range * 1e-10)
		v = 0;
	num::Format(v, out, cap, 8);
}

void App::Analyse(char what)
{
	m_mark = false;
	m_msgError = false;
	if (m_traceFn < 0) {
		StrCopy(m_msg, "There is no graph yet. Press F2 to type a function.", sizeof m_msg);
		m_msgError = true;
		return;
	}
	char xs[48], ys[48], name[4] = "y1";
	name[1] = (char)('1' + m_traceFn);
	double x;
	bool found = false;
	if (what == 'r') {
		found = FindRoot(m_traceFn, -1, &x);
		if (found) {
			FormatFound(x, m_xmax - m_xmin, xs, sizeof xs);
			StrCopy(m_msg, "Zero of ", sizeof m_msg);
			StrAppend(m_msg, name, sizeof m_msg);
			StrAppend(m_msg, " at x = ", sizeof m_msg);
			StrAppend(m_msg, xs, sizeof m_msg);
		} else
			StrCopy(m_msg, "No zero on the screen. Try moving or zooming out.", sizeof m_msg);
	} else if (what == 'n' || what == 'm') {
		found = FindExtremum(m_traceFn, what == 'm', &x);
		if (found) {
			FormatFound(x, m_xmax - m_xmin, xs, sizeof xs);
			FormatFound(F(m_traceFn, x), m_ymax - m_ymin, ys, sizeof ys);
			StrCopy(m_msg, what == 'm' ? "Maximum of " : "Minimum of ", sizeof m_msg);
			StrAppend(m_msg, name, sizeof m_msg);
			StrAppend(m_msg, ": x = ", sizeof m_msg);
			StrAppend(m_msg, xs, sizeof m_msg);
			StrAppend(m_msg, ",  y = ", sizeof m_msg);
			StrAppend(m_msg, ys, sizeof m_msg);
		} else
			StrCopy(m_msg, what == 'm' ? "No maximum on the screen." : "No minimum on the screen.",
				sizeof m_msg);
	} else if (what == 'i') {
		int other = -1;
		for (int k = 1; k < expr::kFunctions; k++) {
			int g = (m_traceFn + k) % expr::kFunctions;
			if (Visible(g)) {
				other = g;
				break;
			}
		}
		if (other < 0) {
			StrCopy(m_msg, "Meeting points need two graphs. Type another on F2.", sizeof m_msg);
			m_msgError = true;
			return;
		}
		char oname[4] = "y1";
		oname[1] = (char)('1' + other);
		found = FindRoot(m_traceFn, other, &x);
		if (found) {
			FormatFound(x, m_xmax - m_xmin, xs, sizeof xs);
			FormatFound(F(m_traceFn, x), m_ymax - m_ymin, ys, sizeof ys);
			StrCopy(m_msg, name, sizeof m_msg);
			StrAppend(m_msg, " and ", sizeof m_msg);
			StrAppend(m_msg, oname, sizeof m_msg);
			StrAppend(m_msg, " meet at x = ", sizeof m_msg);
			StrAppend(m_msg, xs, sizeof m_msg);
			StrAppend(m_msg, ",  y = ", sizeof m_msg);
			StrAppend(m_msg, ys, sizeof m_msg);
		} else {
			StrCopy(m_msg, name, sizeof m_msg);
			StrAppend(m_msg, " and ", sizeof m_msg);
			StrAppend(m_msg, oname, sizeof m_msg);
			StrAppend(m_msg, " don't meet on the screen.", sizeof m_msg);
		}
	}
	if (!found) {
		m_msgError = true;
		return;
	}
	m_traceX = x;
	m_mark = true;
	m_markX = x;
	m_markY = F(m_traceFn, x);
}

// ---------------------------------------------------------------------------
// Graph keys and drawing

void App::KeyGraph(const KeyEvent &e, char ch)
{
	if (m_gotoOpen) {
		if (e.usage == KeyEscape) {
			m_gotoOpen = false;
			return;
		}
		if (e.usage == KeyEnter || e.usage == KeyPadEnter) {
			bool ok;
			char err[80];
			double v = EvalText(m_gotoEdit.text, &ok, err, sizeof err);
			m_gotoOpen = false;
			if (!ok) {
				StrCopy(m_msg, err, sizeof m_msg);
				m_msgError = true;
				return;
			}
			m_msg[0] = 0;
			m_mark = false;
			if (m_screen == ScrTable) {
				m_tblStart = v;
				m_tblSel = m_tblTop = 0;
				Changed();
				return;
			}
			if (v < m_xmin || v > m_xmax) {
				double w = m_xmax - m_xmin;
				SetView(v - w / 2, v + w / 2, m_ymin, m_ymax);
			}
			MoveTrace(v);
			return;
		}
		EditKey(m_gotoEdit, e, ch);
		return;
	}
	bool shift = (e.mods & ModShift) != 0, ctrl = (e.mods & ModCtrl) != 0;
	double w = m_xmax - m_xmin, h = m_ymax - m_ymin;
	if (ctrl) {
		switch (e.usage) {
		case KeyLeft: SetView(m_xmin - w / 8, m_xmax - w / 8, m_ymin, m_ymax); break;
		case KeyRight: SetView(m_xmin + w / 8, m_xmax + w / 8, m_ymin, m_ymax); break;
		case KeyUp: SetView(m_xmin, m_xmax, m_ymin + h / 8, m_ymax + h / 8); break;
		case KeyDown: SetView(m_xmin, m_xmax, m_ymin - h / 8, m_ymax - h / 8); break;
		}
		Changed();
		return;
	}
	switch (e.usage) {
	case KeyLeft:
	case KeyRight: {
		if (m_traceFn < 0)
			FirstTrace();
		double step = TraceStep(PixelW()) * (shift ? 10 : 1);
		double x = m::Round(m_traceX / step) * step + (e.usage == KeyRight ? step : -step);
		m_mark = false;
		m_msg[0] = 0;
		MoveTrace(x);
		Changed();
		return;
	}
	case KeyUp:
	case KeyDown:
		NextTrace(e.usage == KeyUp ? -1 : 1);
		m_mark = false;
		m_msg[0] = 0;
		return;
	case KeyHome:
		StandardView();
		FirstTrace();
		m_mark = false;
		m_msg[0] = 0;
		Changed();
		return;
	case KeyEscape:
		m_mark = false;
		m_msg[0] = 0;
		return;
	case KeyPadPlus:
	case KeyPadMinus:
		break;
	}
	double cx = (m_xmin + m_xmax) / 2, cy = (m_ymin + m_ymax) / 2;
	if (m_traceFn >= 0) {
		double y = F(m_traceFn, m_traceX);
		cx = m_traceX;
		if (m::IsFinite(y) && y >= m_ymin && y <= m_ymax)
			cy = y;
	}
	if (ch >= 'A' && ch <= 'Z')
		ch = (char)(ch + 32);
	switch (ch) {
	case '+':
	case '=':
		ZoomAt(cx, cy, 0.5);
		Changed();
		break;
	case '-':
	case '_':
		ZoomAt(cx, cy, 2);
		Changed();
		break;
	case 'f':
		ZoomFit();
		Changed();
		break;
	case 's':
		SquareView();
		Changed();
		break;
	case 'r':
	case 'n':
	case 'm':
	case 'i':
		Analyse(ch);
		break;
	case 'g':
		m_gotoOpen = true;
		m_gotoEdit.Clear();
		m_msg[0] = 0;
		break;
	default:
		// Typing a number goes to that x.
		if ((ch >= '0' && ch <= '9') || ch == '.') {
			m_gotoOpen = true;
			m_gotoEdit.Clear();
			char s[2] = {ch, 0};
			m_gotoEdit.Insert(s);
			m_msg[0] = 0;
		}
		break;
	}
}

static void Marker(Canvas &c, int x, int y, Color col)
{
	c.FillCircle(x, y, 10, 0xFFFFFFFF);
	c.FillCircle(x, y, 7, col);
}

void App::DrawGraph(Canvas &c)
{
	if (!m_plotValid)
		RenderPlot();
	u32 *dst = c.Pixels() + kPlotY * W;
	MemCopy(dst, m_plot, (u32)(W * kPlotH * 4));
	c.SetClip(0, kPlotY, W, kPlotH);

	bool any = false;
	for (int f = 0; f < expr::kFunctions; f++)
		any |= Visible(f);
	if (!any) {
		c.FillRoundRect(W / 2 - 360, kPlotY + 220, 720, 110, 20, 0xE0FFFFFF);
		c.Text("No graphs yet", font::Title, W / 2, kPlotY + 268, theme::Navy, AlignCenter);
		c.Text("Press F2 and type a function, like x^2 - 3", font::Body, W / 2, kPlotY + 310, theme::InkSoft,
		       AlignCenter);
	}

	char xs[48], ys[48];
	double ty = m_traceFn >= 0 ? F(m_traceFn, m_traceX) : m::Nan();
	if (m_traceFn >= 0) {
		int px = (int)m::Round(ToPx(m_traceX));
		Color col = theme::Fn[m_traceFn];
		c.FillRect(px, kPlotY, 1, kPlotH, WithAlpha(col, 0x60));
		if (m::IsFinite(ty)) {
			double py = ToPy(ty);
			if (py >= -10 && py <= kPlotH + 10) {
				int iy = kPlotY + (int)m::Round(py);
				c.FillRect(0, iy, W, 1, WithAlpha(col, 0x60));
				Marker(c, px, iy, col);
				// Coordinates next to the point.
				num::Format(m_traceX, xs, sizeof xs, 6);
				num::Format(ty, ys, sizeof ys, 6);
				char label[100] = "(";
				StrAppend(label, xs, sizeof label);
				StrAppend(label, ", ", sizeof label);
				StrAppend(label, ys, sizeof label);
				StrAppend(label, ")", sizeof label);
				int lw = Canvas::Measure(label, font::Small) + 20;
				int lx = px + 16 + lw > W ? px - 16 - lw : px + 16;
				int ly = Clamp(iy - 52, kPlotY + 8, kPlotY + kPlotH - 40);
				c.FillRoundRect(lx, ly, lw, 34, 10, 0xEEFFFFFF);
				c.StrokeRoundRect(lx, ly, lw, 34, 10, 2, col);
				c.Text(label, font::Small, lx + 10, ly + 25, theme::Ink);
			}
		}
	}
	if (m_mark) {
		double py = ToPy(m_markY);
		if (m::IsFinite(py) && py > -10 && py < kPlotH + 10) {
			int mx = (int)m::Round(ToPx(m_markX)), my = kPlotY + (int)m::Round(py);
			c.StrokeRoundRect(mx - 15, my - 15, 30, 30, 15, 3, theme::Ink);
		}
	}
	// Message or the "go to x" prompt, at the top of the graph.
	if (m_gotoOpen || m_msg[0]) {
		int bw = 900, bx = (W - bw) / 2, by = kPlotY + 14;
		c.FillRoundRect(bx, by, bw, 50, 14, 0xF0FFFFFF);
		c.StrokeRoundRect(bx, by, bw, 50, 14, 3, m_gotoOpen ? theme::Accent : m_msgError ? theme::Bad : theme::Navy);
		if (m_gotoOpen) {
			c.Text("Go to x =", font::Body, bx + 24, by + 36, theme::InkSoft);
			DrawEdit(c, m_gotoEdit, font::Body, bx + 170, by + 36, bw - 200, true, theme::Ink);
		} else
			c.Text(m_msg, font::Body, W / 2, by + 36, m_msgError ? theme::Bad : theme::Ink, AlignCenter);
	}
	c.ResetClip();

	// Status bar: the traced function and the cursor's coordinates.
	int sy = kPlotY + kPlotH;
	c.FillRect(0, sy, W, H - 44 - sy, theme::Panel);
	c.FillRect(0, sy, W, 1, theme::Line);
	if (m_traceFn >= 0) {
		Color col = theme::Fn[m_traceFn];
		c.FillCircle(34, sy + 20, 10, col);
		char fn[160] = "y1 = ";
		fn[1] = (char)('1' + m_traceFn);
		StrAppend(fn, m_fn[m_traceFn].edit.text, sizeof fn);
		c.SetClip(0, sy, 700, 40);
		c.Text(fn, font::Body, 56, sy + 30, theme::Ink);
		c.ResetClip();
		num::Format(m_traceX, xs, sizeof xs, 8);
		num::Format(ty, ys, sizeof ys, 8);
		char pos[120] = "x = ";
		StrAppend(pos, xs, sizeof pos);
		StrAppend(pos, "      y = ", sizeof pos);
		StrAppend(pos, ys, sizeof pos);
		c.Text(pos, font::Body, W - 24, sy + 30, theme::Ink, AlignRight);
	}
	DrawFooter(c, "\xE2\x86\x90\xE2\x86\x92 trace  \xE2\x86\x91\xE2\x86\x93 graph  +\xE2\x88\x92 zoom  "
		      "R zero  N min  M max  I meet  G go to  F fit");
}

// ---------------------------------------------------------------------------
// Table

static const int kTblRowH = 48, kTblTop = App::kTabH + 70;
static int TableRows() { return (App::H - 44 - kTblTop - 10) / kTblRowH; }

void App::KeyTable(const KeyEvent &e, char ch)
{
	if (m_gotoOpen) {
		KeyGraph(e, ch);  // same prompt; sets the table start here
		return;
	}
	int rows = TableRows();
	switch (e.usage) {
	case KeyUp: m_tblSel--; break;
	case KeyDown: m_tblSel++; break;
	case KeyPageUp: m_tblSel -= rows, m_tblTop -= rows; break;
	case KeyPageDown: m_tblSel += rows, m_tblTop += rows; break;
	case KeyHome: m_tblSel = m_tblTop = 0; break;
	default:
		if (ch == 'g' || ch == 'G' || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
			m_gotoOpen = true;
			m_gotoEdit.Clear();
			if (ch != 'g' && ch != 'G') {
				char s[2] = {ch, 0};
				m_gotoEdit.Insert(s);
			}
		}
		return;
	}
	if (m_tblSel < m_tblTop)
		m_tblTop = m_tblSel;
	if (m_tblSel >= m_tblTop + rows)
		m_tblTop = m_tblSel - rows + 1;
}

void App::DrawTable(Canvas &c)
{
	int cols[expr::kFunctions], n = 0;
	for (int f = 0; f < expr::kFunctions; f++)
		if (Visible(f))
			cols[n++] = f;
	int xW = 260;
	int colW = n ? Min(320, (W - 40 - xW) / n) : 0;
	int left = (W - xW - n * colW) / 2;
	// Header.
	c.FillRoundRect(left - 10, kTabH + 14, xW + n * colW + 20, 48, 12, theme::Navy);
	c.Text("x", font::Title, left + xW / 2, kTabH + 52, 0xFFFFFFFF, AlignCenter);
	for (int i = 0; i < n; i++) {
		char name[4] = "y1";
		name[1] = (char)('1' + cols[i]);
		int cx = left + xW + i * colW + colW / 2;
		c.FillCircle(cx - 34, kTabH + 38, 9, theme::Fn[cols[i]]);
		c.Text(name, font::Title, cx - 18, kTabH + 52, 0xFFFFFFFF);
	}
	int rows = TableRows();
	char s[48];
	for (int r = 0; r < rows; r++) {
		int idx = m_tblTop + r;
		int y = kTblTop + r * kTblRowH;
		double x = m_tblStart + idx * m_tblStep;
		bool sel = idx == m_tblSel;
		c.FillRect(left - 10, y, xW + n * colW + 20, kTblRowH, sel ? theme::Highlight : (r % 2 ? theme::Bg : theme::Panel));
		num::Format(x, s, sizeof s, 8);
		c.Text(s, font::Body, left + xW / 2, y + 34, theme::Ink, AlignCenter);
		for (int i = 0; i < n; i++) {
			double v = F(cols[i], x);
			num::Format(v, s, sizeof s, 8);
			font::Size size = Canvas::Measure(s, font::Body) > colW - 16 ? font::Small : font::Body;
			c.Text(s, size, left + xW + i * colW + colW / 2, y + 34, m::IsFinite(v) ? theme::Ink : theme::Faint,
			       AlignCenter);
		}
	}
	for (int i = 0; i < n; i++)
		c.FillRect(left + xW + i * colW, kTblTop, 1, rows * kTblRowH, theme::Line);
	if (!n) {
		c.Text("No functions to show", font::Title, W / 2, 330, theme::InkSoft, AlignCenter);
		c.Text("Press F2 and type one, like x^2 - 3", font::Body, W / 2, 380, theme::Faint, AlignCenter);
	}
	if (m_gotoOpen || m_msg[0]) {
		int bw = 760, bx = (W - bw) / 2, by = H - 44 - 70;
		c.FillRoundRect(bx, by, bw, 54, 14, theme::Panel);
		c.StrokeRoundRect(bx, by, bw, 54, 14, 3, m_gotoOpen ? theme::Accent : theme::Bad);
		if (m_gotoOpen) {
			c.Text("Start the table at x =", font::Body, bx + 24, by + 38, theme::InkSoft);
			DrawEdit(c, m_gotoEdit, font::Body, bx + 330, by + 38, bw - 350, true, theme::Ink);
		} else
			c.Text(m_msg, font::Body, W / 2, by + 38, theme::Bad, AlignCenter);
	}
	char hint[128] = "\xE2\x86\x91\xE2\x86\x93 scroll    PgUp PgDn page    G start at    Home back    step ";
	num::Format(m_tblStep, s, sizeof s, 8);
	StrAppend(hint, s, sizeof hint);
	StrAppend(hint, " (F5)", sizeof hint);
	DrawFooter(c, hint);
}
