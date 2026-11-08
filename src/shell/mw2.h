/* The shell's database, DATABASE.MW2 (TMPackDataBase; docs/formats/mw2.md): an offset table, the entries
   read by their index from 1, whole (the frame sets, the fonts, the waves) or through the LZSS (the PCX
   screens). */
#pragma once
#include <stdint.h>

typedef struct Mw2 Mw2;

Mw2 *Mw2_Open(const char *name);   /* i76shell 0x1001a510: NULL if it cannot be opened */
void Mw2_Close(Mw2 *db);           /* i76shell 0x1001a710 */
int Mw2_Count(const Mw2 *db);
/* i76shell 0x1001a7d0: entry i (1 to the count) whole, in a block of its own (free()); NULL with size 0
   outside them */
uint8_t *Mw2_Raw(Mw2 *db, int i, uint32_t *size);
/* i76shell 0x1001a8b0: entry i unpacked (its first dword the size, then the LZSS); NULL outside them */
uint8_t *Mw2_Unpack(Mw2 *db, int i, uint32_t *size);
