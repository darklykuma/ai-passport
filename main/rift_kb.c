// rift_kb.c — RIFT BOARD on-device keyboard model (see rift_kb.h).
// Three keys cannot scroll two dimensions comfortably, so each mode page is
// a linear cell list: UP/DOWN walk it with wraparound (hold repeats feed
// ±1), OK activates. The special row carries space/delete/mode/done.
#include "rift_kb.h"

#include <stddef.h>

/* Letter pages are 10/9/7 qwerty rows; the symbol page is three full rows.
 * Row 4 is the special cells on every page. */
static const char *const LAYOUT[RIFT_KB_MODE_COUNT][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"1234567890", "!@#$%^&*()", "-_=+.,?/;:"},
};
static const int ROW_LEN[RIFT_KB_MODE_COUNT][4] = {
    {10, 9, 7, 4},
    {10, 9, 7, 4},
    {10, 10, 10, 4},
};

static struct {
    rift_kb_mode_t mode;
    int cursor;
    char text[RIFT_KB_TEXT_MAX + 1];
    int len;
} s_kb;

void rift_kb_reset(void) {
    s_kb.mode = RIFT_KB_LOWER;
    s_kb.cursor = 0;
    s_kb.len = 0;
    s_kb.text[0] = '\0';
}

int rift_kb_row_count(void) { return 4; }

int rift_kb_row_cells(int row) {
    if (row < 0 || row >= 4) return 0;
    return ROW_LEN[s_kb.mode][row];
}

int rift_kb_cell_count(void) {
    int total = 0;
    for (int row = 0; row < 4; row++) total += ROW_LEN[s_kb.mode][row];
    return total;
}

const rift_kb_cell_t *rift_kb_cell(int index) {
    static rift_kb_cell_t cell;
    int count = rift_kb_cell_count();
    if (index < 0 || index >= count) return NULL;
    int base = 0;
    for (int row = 0; row < 3; row++) {
        int len = ROW_LEN[s_kb.mode][row];
        if (index < base + len) {
            cell.kind = RIFT_KC_CHAR;
            cell.ch = LAYOUT[s_kb.mode][row][index - base];
            return &cell;
        }
        base += len;
    }
    /* Special row order: space, delete, mode, done. */
    static const rift_kb_cell_kind_t SPECIAL[4] = {
        RIFT_KC_SPACE, RIFT_KC_DEL, RIFT_KC_MODE, RIFT_KC_DONE};
    cell.kind = SPECIAL[index - base];
    cell.ch = '\0';
    return &cell;
}

int rift_kb_cursor(void) { return s_kb.cursor; }

void rift_kb_move(int delta) {
    int count = rift_kb_cell_count();
    s_kb.cursor = ((s_kb.cursor + delta) % count + count) % count;
}

rift_kb_mode_t rift_kb_mode(void) { return s_kb.mode; }

const char *rift_kb_text(void) { return s_kb.text; }

int rift_kb_len(void) { return s_kb.len; }

rift_kb_action_t rift_kb_press(void) {
    const rift_kb_cell_t *cell = rift_kb_cell(s_kb.cursor);
    if (!cell) return RIFT_KB_NONE;
    switch (cell->kind) {
    case RIFT_KC_CHAR:
    case RIFT_KC_SPACE:
        if (s_kb.len >= RIFT_KB_TEXT_MAX) return RIFT_KB_NONE;
        s_kb.text[s_kb.len++] =
            cell->kind == RIFT_KC_SPACE ? ' ' : cell->ch;
        s_kb.text[s_kb.len] = '\0';
        return RIFT_KB_INSERTED;
    case RIFT_KC_DEL:
        if (s_kb.len == 0) return RIFT_KB_NONE;
        s_kb.text[--s_kb.len] = '\0';
        return RIFT_KB_DELETED;
    case RIFT_KC_MODE:
        s_kb.mode = (rift_kb_mode_t)((s_kb.mode + 1) % RIFT_KB_MODE_COUNT);
        if (s_kb.cursor >= rift_kb_cell_count()) s_kb.cursor = 0;
        return RIFT_KB_MODE_CHANGED;
    case RIFT_KC_DONE:
    default:
        return s_kb.len > 0 ? RIFT_KB_DONE : RIFT_KB_NONE;
    }
}
