#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdio.h>

/*
 * Every setting in directcrt.ini, in the file's order: its key, default and
 * help text, and how the settings OSD shows it. The default directcrt.ini is
 * written from this table, so the file and the OSD say the same thing.
 * Reading the settings stays in config.c.
 */

typedef enum SETTING_KIND
{
    SETTING_TEXT,    /* not a setting: lines of the ini written as they are */
    SETTING_CHOICE,  /* one of 'choices' */
    SETTING_BOOL,    /* 'choices' is "false-word|true-word", e.g. "off|on" */
    SETTING_INT,     /* min to max, or one of 'choices' (e.g. "auto") */
    SETTING_STRING,  /* free text, or one of 'choices' */
    SETTING_KEY,     /* a virtual-key code, written as 0x.. */
    SETTING_HEADING  /* not a setting: a heading in the OSD's tab ('label'; 'help' is its tooltip) */
} SETTING_KIND;

/* When a change in the OSD takes effect */
typedef enum SETTING_APPLY
{
    SETTING_RESTART, /* when the game starts again */
    SETTING_LIVE,    /* at once */
    SETTING_PICTURE  /* at once; the picture is set up again (shader and colours) */
} SETTING_APPLY;

/* flags */
#define SETTING_PACKED    1 /* no blank line after it in the ini */
#define SETTING_COMMENTED 2 /* written commented out (";key=value") */
#define SETTING_JOINED    4 /* the OSD shows it on the row of the setting before */
#define SETTING_GATE      8 /* SETTING_BOOL: while it's off, the OSD greys out the rest of its tab */

typedef struct SETTING
{
    SETTING_KIND kind;
    const char* key;     /* NULL for SETTING_TEXT */
    const char* value;   /* the default, as written in the ini */

    /* The comment above it in the ini, lines separated by \n, without "; ".
       A line starting with two spaces continues the one before. For
       SETTING_TEXT, the lines as they are */
    const char* help;

    const char* tab;     /* the OSD's tab; NULL = not in the OSD */
    const char* label;   /* the OSD's name for it */

    /* SETTING_CHOICE, SETTING_BOOL, and the named values of SETTING_INT and
       SETTING_STRING: "value|value", or "value=Label|..." when the OSD shows
       a value by another name */
    const char* choices;
    int min, max;        /* SETTING_INT */

    SETTING_APPLY apply;
    int flags;
    int step;            /* SETTING_INT: the OSD's slider moves in these steps (a typed value needn't) */
} SETTING;

int settings_count(void);
const SETTING* settings_get(int i);
const SETTING* settings_find(const char* key);
int settings_index(const char* key); /* -1 if there's no such setting */

/* The tabs of the OSD, in order; NULL after the last */
const char* settings_tab(int i);

/* The i-th of a setting's choices: the value written to the ini and the OSD's
   name for it. FALSE after the last */
int settings_choice(const SETTING* s, int i, char* value, size_t value_size, char* label, size_t label_size);

/* The help's lines one at a time, a line and the ones continuing it (starting
   with two spaces) joined. *cursor starts at s->help; FALSE after the last */
int settings_help_line(const char** cursor, char* buf, size_t size);

/* The help for one of a setting's choices: its lines "value = text" (or
   "a, b = text"), only the text. value = NULL: the help for a value of its
   own (a number or text), the lines "format = text" whose format isn't one of
   the choices. FALSE if the help has none */
int settings_choice_help(const SETTING* s, const char* value, char* buf, size_t size);

/* The default directcrt.ini */
void settings_write_ini(FILE* fh);

#endif
