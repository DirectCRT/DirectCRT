#ifndef VBEAM_H
#define VBEAM_H

#include <windows.h>

/*
 * The virtual CRT beam: one fixed timeline for the emulated display's refresh,
 * read from the high-resolution counter. Every refresh is an active part (the
 * beam draws lines 0 to height-1) followed by the vertical blank, which takes
 * VGA's share of the frame: 45 of 525 lines.
 *
 * WaitForVerticalBlank, GetVerticalBlankStatus, GetScanLine and Flip all read
 * this one beam, so a game sees the same display whichever call it uses.
 */

#define VBEAM_VGA_TOTAL_LINES 525
#define VBEAM_VGA_ACTIVE_LINES 480

/* Pure timeline arithmetic; t, period and active are in counter ticks from the
   beam's origin. The vertical blank of refresh k runs from k*period + active
   to (k+1)*period. */
LONGLONG vbeam_next_begin(LONGLONG t, LONGLONG period, LONGLONG active); /* first vblank start after t */
LONGLONG vbeam_next_end(LONGLONG t, LONGLONG period);                    /* first vblank end after t */
DWORD vbeam_line_at(LONGLONG t, LONGLONG period, DWORD total_lines);     /* the line the beam is on */
DWORD vbeam_total_lines(DWORD active_lines);                             /* active lines plus VGA's share of blank */

void vbeam_init(void);
void vbeam_set_rate(DWORD hz);
DWORD vbeam_rate(void);
void vbeam_set_lines(DWORD active_lines); /* the game's mode height */

BOOL vbeam_in_vblank(void);
DWORD vbeam_scanline(BOOL* in_vblank);

/* Sleeps until the next vertical blank begins (or ends, if end is TRUE). */
void vbeam_wait(BOOL end);

/* A flip is shown at the next vertical blank. This waits only while an earlier
   flip is still pending, the way Flip with DDFLIP_WAIT behaves on hardware. */
void vbeam_flip(void);

/* TRUE until the last flip is shown. */
BOOL vbeam_flip_pending(void);

/* Until a flip is shown, the buffer it replaces is still on screen, so drawing
   into that buffer waits for the flip, as on hardware. */
void vbeam_wait_flip_shown(void);

#endif
