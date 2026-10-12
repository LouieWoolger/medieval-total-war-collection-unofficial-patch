#ifndef MTW_MOVIE_BACKING_FIX_H
#define MTW_MOVIE_BACKING_FIX_H

#include <windows.h>

/* Installs only for the four exact supported EXE files. Safe to call again. */
int mtw_movie_backing_install_once(void);

/* Read-only diagnostics: 0 installed, 1 pitch repairs, 2 capacity growths,
 * 3 failed growth allocations, 4 exact executable accepted, 5 fallback hits. */
DWORD mtw_movie_backing_status(DWORD field);

#endif
