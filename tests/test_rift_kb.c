// tests/test_rift_kb.c — host tests for the on-device keyboard model.
/* Asserts are the test: never let NDEBUG (e.g. zig cc -O1) strip them. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/rift_kb.c"

/* Walks the selection onto the first cell matching kind/ch, then presses. */
static rift_kb_action_t select_and_press(rift_kb_cell_kind_t kind, char ch) {
    int count = rift_kb_cell_count();
    for (int i = 0; i < count; i++) {
        const rift_kb_cell_t *cell = rift_kb_cell(i);
        if (cell->kind == kind
            && (kind != RIFT_KC_CHAR || cell->ch == ch)) {
            rift_kb_move(i - rift_kb_cursor());
            return rift_kb_press();
        }
    }
    assert(false && "cell not found");
    return RIFT_KB_NONE;
}

static void test_layout(void) {
    rift_kb_reset();
    /* letters: 26 chars + 4 specials across 10/9/7/4 */
    assert(rift_kb_cell_count() == 30);
    assert(rift_kb_row_cells(0) == 10 && rift_kb_row_cells(1) == 9);
    assert(rift_kb_row_cells(2) == 7 && rift_kb_row_cells(3) == 4);
    assert(rift_kb_cell(0)->kind == RIFT_KC_CHAR
           && rift_kb_cell(0)->ch == 'q');
    assert(rift_kb_cell(26)->kind == RIFT_KC_SPACE);
    assert(rift_kb_cell(29)->kind == RIFT_KC_DONE);
    assert(rift_kb_cell(30) == NULL && rift_kb_cell(-1) == NULL);
    /* symbols: three full rows of 10 + 4 specials */
    select_and_press(RIFT_KC_MODE, 0);           // -> UPPER
    select_and_press(RIFT_KC_MODE, 0);           // -> SYM
    assert(rift_kb_mode() == RIFT_KB_SYM);
    assert(rift_kb_cell_count() == 34);
    assert(rift_kb_cell(10)->ch == '!' && rift_kb_cell(19)->ch == ')');
    assert(rift_kb_cell(20)->ch == '-' && rift_kb_cell(29)->ch == ':');
}

static void test_typing_and_wrap(void) {
    rift_kb_reset();
    /* cursor wraps backwards past the start */
    rift_kb_move(-1);
    assert(rift_kb_cursor() == rift_kb_cell_count() - 1);
    rift_kb_move(1);
    assert(rift_kb_cursor() == 0);
    /* select-and-press types through the public API only */
    assert(select_and_press(RIFT_KC_CHAR, 'q') == RIFT_KB_INSERTED);
    assert(select_and_press(RIFT_KC_CHAR, 'z') == RIFT_KB_INSERTED);
    assert(rift_kb_len() == 2 && strcmp(rift_kb_text(), "qz") == 0);
    assert(select_and_press(RIFT_KC_SPACE, 0) == RIFT_KB_INSERTED);
    assert(strcmp(rift_kb_text(), "qz ") == 0);
    /* delete on an empty buffer is a no-op; else it drops the last char */
    assert(select_and_press(RIFT_KC_DEL, 0) == RIFT_KB_DELETED);
    assert(select_and_press(RIFT_KC_DEL, 0) == RIFT_KB_DELETED);
    assert(select_and_press(RIFT_KC_DEL, 0) == RIFT_KB_DELETED);
    assert(rift_kb_len() == 0);
    /* done needs content */
    assert(select_and_press(RIFT_KC_DONE, 0) == RIFT_KB_NONE);
}

static void test_upper_and_full_buffer(void) {
    rift_kb_reset();
    assert(select_and_press(RIFT_KC_MODE, 0) == RIFT_KB_MODE_CHANGED);
    assert(rift_kb_mode() == RIFT_KB_UPPER);
    assert(select_and_press(RIFT_KC_CHAR, 'A') == RIFT_KB_INSERTED);
    assert(strcmp(rift_kb_text(), "A") == 0);
    /* mode switch clamps a cursor past the shorter page */
    select_and_press(RIFT_KC_MODE, 0);           // -> SYM (34 cells)
    rift_kb_move(33);                            // cursor 33
    select_and_press(RIFT_KC_MODE, 0);           // -> LOWER (30 cells)
    assert(rift_kb_cursor() == 0);
    assert(rift_kb_mode() == RIFT_KB_LOWER);
    /* fill to the cap: the 65th character is refused */
    rift_kb_reset();
    for (int i = 0; i < RIFT_KB_TEXT_MAX; i++)
        assert(select_and_press(RIFT_KC_CHAR, 'k') == RIFT_KB_INSERTED);
    assert(rift_kb_len() == RIFT_KB_TEXT_MAX);
    assert(select_and_press(RIFT_KC_CHAR, 'k') == RIFT_KB_NONE);
    assert(rift_kb_len() == RIFT_KB_TEXT_MAX);
    assert(select_and_press(RIFT_KC_SPACE, 0) == RIFT_KB_NONE);
    /* cycle covers all three modes and lands back */
    rift_kb_reset();
    for (int i = 0; i < 3; i++)
        assert(select_and_press(RIFT_KC_MODE, 0) == RIFT_KB_MODE_CHANGED);
    assert(rift_kb_mode() == RIFT_KB_LOWER);
}

int main(void) {
    test_layout();
    test_typing_and_wrap();
    test_upper_and_full_buffer();
    printf("test_rift_kb: all assertions passed\n");
    return 0;
}
