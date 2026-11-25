/* Unit tests for src/settings.c: the table agrees with the code that reads it
 *
 * Build and run: tests\run_tests.cmd (from a Visual Studio developer prompt)
 *
 * crt_auto.c keeps DOSBox Staging's defaults and ranges, and config.c reads
 * the choices by their places; settings.c writes them into the ini and the
 * OSD. These checks keep the two the same.
 */

#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "crt_auto.h"
#include "overlay.h"
#include "check.h"

/* crt-auto's settings: settings.c's default and range are crt_auto.c's, and
 * crt_auto.c takes every choice the OSD offers */
static void test_crt_auto_settings(void)
{
    for (int i = 0; i < crt_auto_setting_count(); i++)
    {
        const char* name = crt_auto_setting_name(i);
        const SETTING* s = settings_find(name);
        CHECK(s != NULL);

        if (!s)
        {
            printf("  %s isn't in settings.c\n", name);
            continue;
        }

        /* The same, or what crt-auto's 'auto' is for VGA (settings.c writes
           that, DirectCRT being VGA only): the same colours */
        if (strcmp(s->value, crt_auto_setting_default(i)) != 0)
        {
            CRT_AUTO_SETTINGS defaults = crt_auto_default_settings();
            CRT_AUTO_SETTINGS written = defaults;
            CHECK(crt_auto_set(&written, name, s->value));

            CRT_AUTO_COLOR a = crt_auto_vga_color(&defaults);
            CRT_AUTO_COLOR b = crt_auto_vga_color(&written);
            int same = memcmp(&a, &b, sizeof(a)) == 0;
            CHECK(same);

            if (!same)
                printf("  %s=%s isn't crt-auto's %s\n", name, s->value, crt_auto_setting_default(i));
        }

        int min, max;

        if (crt_auto_setting_range(i, &min, &max))
        {
            CHECK(s->kind == SETTING_INT);
            CHECK(s->min == min);
            CHECK(s->max == max);
        }

        char value[64], label[64];

        for (int c = 0; settings_choice(s, c, value, sizeof(value), label, sizeof(label)); c++)
        {
            CRT_AUTO_SETTINGS settings = crt_auto_default_settings();
            int ok = crt_auto_set(&settings, name, value);
            CHECK(ok);

            if (!ok)
                printf("  %s=%s\n", name, value);
        }
    }
}

/* config.c reads a choice by its place, and falls back on the default */
static void test_choices(void)
{
    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (s->kind != SETTING_CHOICE)
            continue;

        char value[64], label[64];
        int found = 0;

        for (int c = 0; settings_choice(s, c, value, sizeof(value), label, sizeof(label)); c++)
            found |= _stricmp(value, s->value) == 0;

        CHECK(found);

        if (!found)
            printf("  %s's default %s isn't one of its choices\n", s->key, s->value);
    }

    /* status_osd_position's places are OVERLAY_CORNER's */
    static const struct
    {
        int corner;
        const char* value;
    } corners[] = {
        { OVERLAY_TOP_LEFT, "top-left" },
        { OVERLAY_TOP_RIGHT, "top-right" },
        { OVERLAY_BOTTOM_LEFT, "bottom-left" },
        { OVERLAY_BOTTOM_RIGHT, "bottom-right" },
    };

    const SETTING* position = settings_find("status_osd_position");
    CHECK(position != NULL);

    for (size_t i = 0; position && i < sizeof(corners) / sizeof(corners[0]); i++)
    {
        char value[64], label[64];
        CHECK(settings_choice(position, corners[i].corner, value, sizeof(value), label, sizeof(label)));
        CHECK_STR(value, corners[i].value);
    }

    /* status_osd_size's: small, medium, large (overlay_status.c's sizes) */
    const SETTING* size = settings_find("status_osd_size");
    CHECK(size != NULL);
    CHECK(size && size->choices && strncmp(size->choices, "small|medium|large", 18) == 0);

    /* status_osd_detail's: full is the second */
    const SETTING* detail = settings_find("status_osd_detail");
    CHECK(detail != NULL);
    CHECK(detail && detail->choices && strcmp(detail->choices, "basic|full") == 0);
}

/* The OSD's tints have names; settings have keys */
static void test_rows(void)
{
    for (int i = 0; i < settings_count(); i++)
    {
        const SETTING* s = settings_get(i);

        if (s->kind != SETTING_TEXT && s->kind != SETTING_HEADING)
        {
            CHECK(s->key != NULL);
            CHECK(settings_find(s->key) == s);
        }

        CHECK(!s->tint || s->tint_name);
    }
}

int main(void)
{
    test_crt_auto_settings();
    test_choices();
    test_rows();
    return check_report("settings");
}
