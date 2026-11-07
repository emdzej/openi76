/* The mission's objectives (0x41e5e0-0x41f2c0; docs/re/fsm.md "The objectives"): the text file the mission
   names (.npt, World +0x45) gives up to six objectives, each perhaps "[hidden]", then after "[failure]" up
   to sixteen failure texts. The script reveals, succeeds and fails them by number (1-6). The Notepad that
   shows them is not ported yet. */
#pragma once
#include <stdint.h>

typedef struct {
    int32_t count;           /* 0x5ad1e0: the objectives (-1 when the file is missing) */
    int32_t failures;        /* 0x5ad1e4: the failure texts */
    char *text[6];           /* 0x5ad1e8 */
    char *failure[16];       /* 0x5ad200 */
    uint32_t flags[6];       /* 0x5ad240: 1 hidden, 2 succeeded, 4 failed */
    int32_t fail_except;     /* 0x5ad258: failAllObj's objective less one; -1 at the load, -2 after successAll or failAll */
} Objectives;
extern Objectives g_objectives;

/* Objectives_Load 0x41e5e0 (after the mission's load): 1 when read */
int Objectives_Load(void);
/* Objective_Reveal 0x41f0b0, Objective_Succeed 0x41f130, Objective_Fail 0x41f1e0: objective i (1-6); the
   chime (cnote.wav) on a reveal and a success is not ported */
void Objective_Reveal(int32_t i);
void Objective_Succeed(int32_t i);
void Objective_Fail(int32_t i);
/* 0x41f280: the objective failAllObj names, less one (successAll and failAll pass -1: -2) */
void Objectives_FailExcept(int32_t i);
