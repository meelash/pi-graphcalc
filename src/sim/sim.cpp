// Desktop simulator: runs the calculator with scripted key presses and writes
// screenshots, so screens can be checked without a Raspberry Pi.
//
// Usage: sim <script> <outdir> [savedir]
// Script commands (one per line):
//   key <name> [shift|ctrl]  enter esc tab space backspace delete left right up down
//                            home end pgup pgdn f1..f12, or a single character
//   type <text>          types ASCII text on a US keyboard
//   wait <ms>            advances virtual time
//   shot <name>          writes <outdir>/<name>.png
//   expect <screen>      calc | functions | graph | table | settings | help
//   expect-result <t>    the last answer on the Calculate screen
//   expect-status <t>    the message on the graph/table/settings screen contains t
//   menu <on|off>        whether the card has the boot menu (default off)
//   expect-menu-return <n>  how many times the app asked to go back to the menu
//   restart              starts the calculator again (as after power off), same save folder
//   bench                average time to draw a frame of this screen (full redraw)
#include "../core/app.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <zlib.h>

class SimPlatform : public Platform
{
public:
	u32 now = 0;
	std::string saveDir;
	bool menu = false;
	int menuReturns = 0;
	u32 Millis() override { return now; }
	bool KeyboardPresent() override { return true; }
	bool StorageAvailable() override { return true; }
	bool HasMenu() override { return menu; }
	void ReturnToMenu() override { menuReturns++; }
	int Diagnostics(const char **lines, int max) override
	{
		static char buf[40][48];
		int n = Min(40, max);
		for (int i = 0; i < n; i++) {
			snprintf(buf[i], sizeof buf[i], "00:00:%02d.00 sim: log line %d", i / 10, i + 1);
			lines[i] = buf[i];
		}
		return n;
	}
	bool LoadFile(const char *name, char *buf, int cap, int *len) override
	{
		FILE *f = fopen((saveDir + "/" + name).c_str(), "rb");
		if (!f)
			return false;
		*len = (int)fread(buf, 1, (size_t)cap, f);
		fclose(f);
		return true;
	}
	bool SaveFile(const char *name, const char *buf, int len) override
	{
		std::string path = saveDir + "/" + name;
		std::string dir = path.substr(0, path.rfind('/'));
		mkdir(dir.c_str(), 0755);
		FILE *f = fopen(path.c_str(), "wb");
		if (!f)
			return false;
		fwrite(buf, 1, (size_t)len, f);
		fclose(f);
		return true;
	}
};

static void WritePng(const char *path, const u32 *px, int w, int h)
{
	std::vector<unsigned char> raw;
	raw.reserve((size_t)(w * 3 + 1) * h);
	for (int y = 0; y < h; y++) {
		raw.push_back(0);
		for (int x = 0; x < w; x++) {
			u32 c = px[y * w + x];
			raw.push_back((c >> 16) & 0xFF);
			raw.push_back((c >> 8) & 0xFF);
			raw.push_back(c & 0xFF);
		}
	}
	uLongf zlen = compressBound(raw.size());
	std::vector<unsigned char> z(zlen);
	compress2(z.data(), &zlen, raw.data(), raw.size(), 6);
	FILE *f = fopen(path, "wb");
	auto be32 = [&](u32 v) {
		unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8),
				      (unsigned char)v};
		fwrite(b, 1, 4, f);
	};
	auto chunk = [&](const char *type, const unsigned char *data, u32 len) {
		be32(len);
		fwrite(type, 1, 4, f);
		if (len)
			fwrite(data, 1, len, f);
		uLong crc = crc32(0, (const Bytef *)type, 4);
		crc = crc32(crc, data, len);
		be32((u32)crc);
	};
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	unsigned char ihdr[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0};
	for (int i = 0; i < 4; i++) {
		ihdr[i] = (unsigned char)(w >> (24 - 8 * i));
		ihdr[4 + i] = (unsigned char)(h >> (24 - 8 * i));
	}
	chunk("IHDR", ihdr, 13);
	chunk("IDAT", z.data(), (u32)zlen);
	chunk("IEND", nullptr, 0);
	fclose(f);
}

static const struct
{
	const char *name;
	u8 usage;
} kKeyNames[] = {
	{"enter", KeyEnter},   {"esc", KeyEscape},  {"tab", KeyTab},        {"space", KeySpace},
	{"backspace", KeyBackspace}, {"delete", KeyDelete}, {"left", KeyLeft}, {"right", KeyRight},
	{"up", KeyUp},         {"down", KeyDown},   {"home", KeyHome},      {"end", KeyEnd},
	{"pgup", KeyPageUp},   {"pgdn", KeyPageDown},
};

static const char *kScreens[] = {"calc", "functions", "graph", "table", "settings", "help"};

// HID usage and shift for an ASCII character on a US keyboard.
static bool UsageFor(char ch, u8 *usage, bool *shift)
{
	for (int u = 0; u < 256; u++)
		for (int s = 0; s < 2; s++)
			if (App::KeyChar(KeyEvent{(u8)u, (u8)(s ? ModLShift : 0)}) == ch && !(u >= KeyPad1 && u <= KeyPadDot) &&
			    u != KeyPadSlash && u != KeyPadStar && u != KeyPadMinus && u != KeyPadPlus) {
				*usage = (u8)u;
				*shift = s;
				return true;
			}
	return false;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: sim <script> <outdir> [savedir]\n");
		return 2;
	}
	SimPlatform plat;
	plat.saveDir = argc > 3 ? argv[3] : argv[2];
	static u32 pixels[App::W * App::H];
	Canvas canvas(pixels, App::W, App::H);
	static App app(&plat);
	app.Init();

	FILE *script = fopen(argv[1], "r");
	if (!script) {
		perror(argv[1]);
		return 2;
	}
	auto step = [&](u32 ms) {
		plat.now += ms;
		app.Update();
		app.Draw(canvas);
	};
	auto press = [&](u8 usage, u8 mods) {
		app.OnKey(KeyEvent{usage, mods});
		step(40);
	};
	char line[1024];
	int lineNo = 0, failures = 0;
	while (fgets(line, sizeof line, script)) {
		lineNo++;
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0] || line[0] == '#')
			continue;
		char *arg = strchr(line, ' ');
		if (arg)
			*arg++ = 0;
		else
			arg = line + strlen(line);
		if (!strcmp(line, "key")) {
			char name[32] = "", mod[16] = "";
			sscanf(arg, "%31s %15s", name, mod);
			u8 mods = !strcmp(mod, "shift") ? ModLShift : !strcmp(mod, "ctrl") ? ModLCtrl : 0;
			u8 usage = 0;
			for (auto &k : kKeyNames)
				if (!strcmp(k.name, name))
					usage = k.usage;
			if (name[0] == 'f' && name[1] >= '1' && name[1] <= '9')
				usage = KeyF(atoi(name + 1));
			bool shift = false;
			if (!usage && name[0] && !name[1] && UsageFor(name[0], &usage, &shift) && shift)
				mods |= ModLShift;
			if (!usage) {
				fprintf(stderr, "%s:%d: unknown key %s\n", argv[1], lineNo, name);
				failures++;
				continue;
			}
			press(usage, mods);
		} else if (!strcmp(line, "type")) {
			for (const char *p = arg; *p; p++) {
				u8 usage;
				bool shift;
				if (!UsageFor(*p, &usage, &shift)) {
					fprintf(stderr, "%s:%d: can't type '%c'\n", argv[1], lineNo, *p);
					failures++;
					continue;
				}
				press(usage, shift ? ModLShift : 0);
			}
		} else if (!strcmp(line, "wait")) {
			for (int ms = atoi(arg); ms > 0; ms -= 33)
				step((u32)Min(ms, 33));
		} else if (!strcmp(line, "shot")) {
			step(16);
			std::string p = std::string(argv[2]) + "/" + arg + ".png";
			WritePng(p.c_str(), pixels, App::W, App::H);
			printf("shot %s\n", p.c_str());
		} else if (!strcmp(line, "expect")) {
			const char *cur = kScreens[app.CurrentScreen()];
			if (strcmp(cur, arg)) {
				fprintf(stderr, "%s:%d: expected screen %s, got %s\n", argv[1], lineNo, arg, cur);
				failures++;
			}
		} else if (!strcmp(line, "expect-result")) {
			if (strcmp(app.LastResult(), arg)) {
				fprintf(stderr, "%s:%d: expected result \"%s\", got \"%s\"\n", argv[1], lineNo, arg,
					app.LastResult());
				failures++;
			}
		} else if (!strcmp(line, "expect-status")) {
			if (!strstr(app.StatusText(), arg)) {
				fprintf(stderr, "%s:%d: expected status with \"%s\", got \"%s\"\n", argv[1], lineNo, arg,
					app.StatusText());
				failures++;
			}
		} else if (!strcmp(line, "menu")) {
			plat.menu = strcmp(arg, "off") != 0;
		} else if (!strcmp(line, "expect-menu-return")) {
			if (plat.menuReturns != atoi(arg)) {
				fprintf(stderr, "%s:%d: expected %s menu returns, got %d\n", argv[1], lineNo, arg,
					plat.menuReturns);
				failures++;
			}
		} else if (!strcmp(line, "restart")) {
			step(4000);  // let the delayed save happen, as a real pause would
			app.Init();
			step(16);
		} else if (!strcmp(line, "bench")) {
			timespec a, b;
			clock_gettime(CLOCK_MONOTONIC, &a);
			for (int i = 0; i < 20; i++) {
				app.OnKey(KeyEvent{0, 0});  // marks the screen for redrawing
				app.Draw(canvas);
			}
			clock_gettime(CLOCK_MONOTONIC, &b);
			double ms = ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / 20;
			printf("bench %s: %.2f ms/frame\n", kScreens[app.CurrentScreen()], ms);
		} else {
			fprintf(stderr, "%s:%d: unknown command %s\n", argv[1], lineNo, line);
			failures++;
		}
	}
	fclose(script);
	step(4000);  // the delayed save, so the next script starts from this one's work
	return failures ? 1 : 0;
}
