/* The CD drive as the exe finds it (0x484bc0, 0x484c20 Cd_FindByLabel, 0x484d10): the disc in the port's
   drive slot (vfs.h, disc 2 as the gasm asset cd2), or with the slot empty the game's own mount standing
   for disc 1 in the drive (the original runs installed, a disc of the game in the drive) */
#pragma once
#include <stdbool.h>

/* the prefix of a file on the disc in the drive: "CD:" or "" (the game's mount) */
const char *Cd_Root(void);
/* 0x484d10 (the shell's +0x44): disc 2 in the drive, its label I76_CD2 (0x4e9b74) and int12f01.smk on it
   (0x4e9b90, through Movie_Exists). With ask the original repeats a message box until it is put in or
   cancelled (0x484cc0); here the message is logged once and the answer is no */
int Cd_Disc2(int ask);
