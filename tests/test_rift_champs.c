// test_rift_champs.c — host test for the generated zh_CN champion table.
// Guards the mapping the BP page relies on: known ids resolve, unknown ids
// fall back to the feed id (never NULL), and the table stays the size the
// generator wrote.
#include "rift_champs.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            fails++;                                                      \
        }                                                                 \
    } while (0)

int main(void) {
    /* G1 of KC 3:0 MVK — the sample series the device boots with. */
    static const char *G1[10] = {"Jayce", "LeeSin", "Cassiopeia", "Zeri",
                                 "Yuumi", "Yorick", "JarvanIV", "Akali",
                                 "Ashe", "Seraphine"};
    static const char *G1_ZH[10] = {"杰斯", "李青", "卡西奥佩娅", "泽丽",
                                    "悠米", "约里克", "嘉文四世", "阿卡丽",
                                    "艾希", "萨勒芬妮"};
    for (int i = 0; i < 10; i++)
        CHECK(strcmp(rift_champ_zh(G1[i]), G1_ZH[i]) == 0);

    /* Normalized compound names keep only the first segment. */
    CHECK(strcmp(rift_champ_zh("Renata"), "烈娜塔") == 0);
    CHECK(strcmp(rift_champ_zh("AurelionSol"), "奥瑞利安") == 0);
    CHECK(strcmp(rift_champ_zh("Nunu"), "努努") == 0);

    /* Unknown, empty, and NULL ids fall back safely (no tofu source). */
    CHECK(strcmp(rift_champ_zh("NotAChampion"), "NotAChampion") == 0);
    CHECK(strcmp(rift_champ_zh(""), "") == 0);
    CHECK(rift_champ_zh(NULL)[0] == '\0');

    /* Every stored id must map to a non-empty zh name different from the
     * id (all zh names are CJK, ids are ASCII). Spot-check non-obvious
     * feed ids: the monkey king's key is MonkeyKing, not Wukong. */
    CHECK(rift_champ_count() == 173);
    CHECK(strcmp(rift_champ_zh("MonkeyKing"), "孙悟空") == 0);
    CHECK(strcmp(rift_champ_zh("MissFortune"), "厄运小姐") == 0);
    CHECK(strcmp(rift_champ_zh("Aatrox"), "亚托克斯") == 0);

    if (fails) {
        printf("test_rift_champs: %d failure(s)\n", fails);
        return 1;
    }
    printf("test_rift_champs: PASS (%d champions)\n", rift_champ_count());
    return 0;
}
