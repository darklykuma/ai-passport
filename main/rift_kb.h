// rift_kb.h — RIFT BOARD on-device keyboard model. Pure C, no ESP-IDF or
// LVGL: layout tables, cursor movement, and edit-buffer rules here are
// covered by tests/test_rift_kb.c on the host. Rendering reads the cells
// through the accessors and draws them as a fixed grid (rift_view.c).
#pragma once

#include <stdbool.h>

#define RIFT_KB_TEXT_MAX 64   // Wi-Fi password cap is 63 + NUL

typedef enum {
    RIFT_KB_LOWER = 0,   // qwerty rows
    RIFT_KB_UPPER,
    RIFT_KB_SYM,         // digits + common password symbols
    RIFT_KB_MODE_COUNT,
} rift_kb_mode_t;

typedef enum {
    RIFT_KC_CHAR = 0,    // printable ASCII, label is cell->ch
    RIFT_KC_SPACE,
    RIFT_KC_DEL,
    RIFT_KC_MODE,        // cycles lower -> upper -> sym
    RIFT_KC_DONE,
} rift_kb_cell_kind_t;

typedef struct {
    rift_kb_cell_kind_t kind;
    char ch;             // RIFT_KC_CHAR only
} rift_kb_cell_t;

typedef enum {
    RIFT_KB_NONE = 0,    // press had no effect (full buffer, empty delete)
    RIFT_KB_INSERTED,
    RIFT_KB_DELETED,
    RIFT_KB_MODE_CHANGED,
    RIFT_KB_DONE,        // [done] fired; text() holds the final string
} rift_kb_action_t;

// Clears the buffer and cursor, selects the lowercase page.
void rift_kb_reset(void);
// One OK press on the selected cell; returns what the caller must do.
rift_kb_action_t rift_kb_press(void);
// Moves the selection with wraparound; hold-to-repeat just calls this.
void rift_kb_move(int delta);

const char *rift_kb_text(void);
int rift_kb_len(void);
rift_kb_mode_t rift_kb_mode(void);
int rift_kb_cursor(void);

// Grid geometry for the renderer: row widths per mode page, linear index =
// sum of preceding row widths + column.
int rift_kb_cell_count(void);
int rift_kb_row_count(void);
int rift_kb_row_cells(int row);
const rift_kb_cell_t *rift_kb_cell(int index);
