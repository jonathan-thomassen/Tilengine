#ifndef _DEBUG_H
#define _DEBUG_H

/* conditional debugmsg() trace */
#ifdef _DEBUG
#include <stdio.h>
#define debugmsg printf
#else
#define debugmsg(format, ...) ((void)0)
#endif

#endif
