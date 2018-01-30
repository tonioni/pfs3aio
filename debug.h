#ifndef DEBUG_H
#define DEBUG_H

#if defined(__SASC) && defined(USE_GLOBALDEBUG)
#include <debug/debug.h>
#endif

#if defined(__GNUC__) && defined(DEBUG)
#define DB(x) x
void ENTER(char*);
void EXIT(char*);
void Trace(int id, char*, char *, ...);
#endif

#ifndef DB
#define DB(x)
#define Trace(x)
#define ENTER(x)
#define EXIT(x)
#endif

#endif /* DEBUG_H */
