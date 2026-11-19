// A one-line text editor (ASCII) with a cursor.
#pragma once
#include "base.h"

struct LineEdit
{
	static const int kCap = 120;
	char text[kCap + 1];
	int len, cur;

	void Clear() { text[0] = 0, len = cur = 0; }
	void Set(const char *s)
	{
		StrCopy(text, s, sizeof text);
		len = cur = StrLen(text);
	}
	bool Insert(const char *s)
	{
		int n = StrLen(s);
		if (len + n > kCap)
			return false;
		for (int i = len; i >= cur; i--)
			text[i + n] = text[i];
		for (int i = 0; i < n; i++)
			text[cur + i] = s[i];
		len += n, cur += n;
		return true;
	}
	void Backspace()
	{
		if (!cur)
			return;
		for (int i = cur - 1; i < len; i++)
			text[i] = text[i + 1];
		len--, cur--;
	}
	void Delete()
	{
		if (cur >= len)
			return;
		for (int i = cur; i < len; i++)
			text[i] = text[i + 1];
		len--;
	}
	void Left() { cur = Max(0, cur - 1); }
	void Right() { cur = Min(len, cur + 1); }
	void Home() { cur = 0; }
	void End() { cur = len; }
	bool Empty() const
	{
		for (int i = 0; i < len; i++)
			if (text[i] != ' ')
				return false;
		return true;
	}
};
