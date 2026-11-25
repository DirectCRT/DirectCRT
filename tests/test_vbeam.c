/* Unit tests for src/vbeam.c
 *
 * Build and run: tests\run_tests.cmd (from a Visual Studio developer prompt)
 *
 * The first tests check the timeline arithmetic; the last ones wait on the
 * real beam, so they take about half a second and use loose time bounds.
 */

#include <stdio.h>
#include <windows.h>
#include "vbeam.h"
#include "check.h"

static void test_total_lines(void)
{
    CHECK(vbeam_total_lines(480) == 525);
    CHECK(vbeam_total_lines(0) == 525);
    CHECK(vbeam_total_lines(600) == 656);
    CHECK(vbeam_total_lines(400) == 438);
    CHECK(vbeam_total_lines(768) == 840);
}

static void test_edges(void)
{
    /* A refresh of 1050 ticks, 960 of them drawing lines and 90 of blank */
    LONGLONG p = 1050, a = 960;

    CHECK(vbeam_next_begin(0, p, a) == 960);
    CHECK(vbeam_next_begin(959, p, a) == 960);
    CHECK(vbeam_next_begin(960, p, a) == 960 + 1050); /* strictly after: one blank is never returned twice */
    CHECK(vbeam_next_begin(1000, p, a) == 2010);
    CHECK(vbeam_next_begin(1050, p, a) == 2010);
    CHECK(vbeam_next_begin(5 * 1050 + 10, p, a) == 5 * 1050 + 960);

    CHECK(vbeam_next_end(0, p) == 1050);
    CHECK(vbeam_next_end(1049, p) == 1050);
    CHECK(vbeam_next_end(1050, p) == 2100);
    CHECK(vbeam_next_end(970, p) == 1050);
}

static void test_lines(void)
{
    LONGLONG p = 1050;

    CHECK(vbeam_line_at(0, p, 525) == 0);
    CHECK(vbeam_line_at(2, p, 525) == 1);
    CHECK(vbeam_line_at(959, p, 525) == 479);
    CHECK(vbeam_line_at(960, p, 525) == 480); /* the first blank line */
    CHECK(vbeam_line_at(1049, p, 525) == 524);
    CHECK(vbeam_line_at(1050, p, 525) == 0);
    CHECK(vbeam_line_at(3 * 1050 + 960, p, 525) == 480);
}

static double seconds(LARGE_INTEGER a, LARGE_INTEGER b)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
}

static void test_real_beam(void)
{
    vbeam_init();
    vbeam_set_lines(480);

    /* After waiting for the blank to begin, the beam is in it */
    vbeam_wait(FALSE);
    BOOL in_vb = FALSE;
    DWORD line = vbeam_scanline(&in_vb);
    CHECK(in_vb);
    CHECK(line >= 480 && line < 525);

    /* After waiting for the blank to end, the beam is near the top */
    vbeam_wait(TRUE);
    line = vbeam_scanline(&in_vb);
    CHECK(!in_vb);
    CHECK(line < 100);

    /* Ten waits are ten refreshes: 1/6 s at 60 Hz */
    LARGE_INTEGER t0, t1;
    vbeam_wait(FALSE);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < 10; i++)
        vbeam_wait(FALSE);
    QueryPerformanceCounter(&t1);
    double s = seconds(t0, t1);
    printf("  10 vblanks at 60 Hz: %.2f ms\n", s * 1000);
    CHECK(s > 0.160 && s < 0.175);

    /* The first flip returns at once; the next waits for the blank that shows it */
    vbeam_wait(TRUE);
    QueryPerformanceCounter(&t0);
    vbeam_flip();
    QueryPerformanceCounter(&t1);
    CHECK(seconds(t0, t1) < 0.002);
    vbeam_flip();
    in_vb = vbeam_in_vblank();
    QueryPerformanceCounter(&t1);
    printf("  second flip returned after %.2f ms\n", seconds(t0, t1) * 1000);
    CHECK(in_vb);
    CHECK(seconds(t0, t1) > 0.013 && seconds(t0, t1) < 0.0175);

    /* Drawing into the buffer a pending flip replaces waits for the flip */
    vbeam_wait(TRUE);
    vbeam_flip();
    CHECK(vbeam_flip_pending());
    QueryPerformanceCounter(&t0);
    vbeam_wait_flip_shown();
    QueryPerformanceCounter(&t1);
    CHECK(vbeam_in_vblank());
    CHECK(!vbeam_flip_pending());
    CHECK(seconds(t0, t1) > 0.013 && seconds(t0, t1) < 0.0175);
    QueryPerformanceCounter(&t0);
    vbeam_wait_flip_shown();
    QueryPerformanceCounter(&t1);
    CHECK(seconds(t0, t1) < 0.001);

    /* A flip long after the last one is shown at once */
    Sleep(50);
    QueryPerformanceCounter(&t0);
    vbeam_flip();
    QueryPerformanceCounter(&t1);
    CHECK(seconds(t0, t1) < 0.002);

    /* A faster rate gives shorter refreshes */
    vbeam_set_rate(85);
    CHECK(vbeam_rate() == 85);
    vbeam_wait(FALSE);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < 10; i++)
        vbeam_wait(FALSE);
    QueryPerformanceCounter(&t1);
    s = seconds(t0, t1);
    printf("  10 vblanks at 85 Hz: %.2f ms\n", s * 1000);
    CHECK(s > 0.115 && s < 0.125);

    vbeam_set_rate(1000);
    CHECK(vbeam_rate() == 85);
}

int main(void)
{
    timeBeginPeriod(1);

    test_total_lines();
    test_edges();
    test_lines();
    test_real_beam();

    return check_report("vbeam");
}
