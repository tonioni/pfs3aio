
#include <exec/types.h>
#include <stdio.h>

extern __stdargs void rawdofmt(char*, char*, void*);

void serout(const char *s)
{
	static int init;
	if (!init) {
		init = 1;
		volatile UWORD *serper = (UWORD*)0xdff032;
		*serper = 10;
	}

	volatile UWORD *serdatr = (UWORD*)0xdff018;
	volatile UWORD *serdat = (UWORD*)0xdff030;
	while (*s) {
		for (;;) {
			UWORD v = *serdatr;
			if (v & 0x1000)
				break;
		}
		*serdat = (*s) | 0x100;
		s++;
	}
}

void DebugPutStr(char *buff)
{
	serout(buff);
}
void DebugPutDec(char *what, ULONG val)
{
	APTR format[2] = { (APTR)val, what };
	char buf[80];
	rawdofmt(buf, "%08ld %s ", format);
	serout(buf);
}
void DebugPutHex(char *what, ULONG val)
{
	APTR format[2] = { (APTR)val, what };
	char buf[80];
	rawdofmt(buf, "%08lx %s ", format);
	serout(buf);
}
void DebugPutHexVal(ULONG val)
{
}

void ENTER(char *s)
{
	serout(">");
	serout(s);
	serout("\n");
}
void EXIT(char *s)
{
	serout("<");
	serout(s);
	serout("\n");
}

void Trace(int id, char *title, char *format, ...)
{
	va_list parms;
	char buf[256];

	serout("-");
	serout(title);
	serout(":");
	va_start(parms, format);
	rawdofmt(buf, format, parms);
	serout(buf);

	va_end(parms);
}

void ErrorTrace(int id, char *title, char *format, ...)
{
	va_list parms;
	char buf[256];

	serout("-");
	serout(title);
	serout(":");
	va_start(parms, format);
	rawdofmt(buf, format, parms);
	serout(buf);

	va_end(parms);
}