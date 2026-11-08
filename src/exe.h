/* The original game program, read at run time: tables the game needs are taken from I76.EXE of the
   user's copy rather than copied into the source tree (AGENTS.md, hard rule 2). Only the supported build
   is accepted: version 1.0 (linked 1997-03-16), 1,019,392 bytes, CRC-32 4129421d. I76SHELL.DLL (the
   front end) gets the same treatment: version 1.0, 471,552 bytes, CRC-32 eee0a66d. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXE_PATH "I76.EXE"
#define EXE_SIZE 1019392u
#define EXE_CRC32 0x4129421du
/* Loads and checks the exe through the file layer. False (err says why) if missing or another build. */
bool exe_init(char *err, size_t errcap);
bool exe_loaded(void);
void exe_free(void);
/* len bytes at virtual address va (image base 0x400000), or NULL if the exe isn't loaded or the range
   isn't initialised data of one section (bytes beyond a section's raw size are zero-fill: NULL). */
const uint8_t *exe_data(uint32_t va, size_t len);
/* Little-endian reads at va; 0 if unavailable. */
uint32_t exe_u32(uint32_t va);
uint16_t exe_u16(uint32_t va);
float exe_f32(uint32_t va);
/* The NUL-terminated string at va, or "" if unavailable. */
const char *exe_str(uint32_t va);

/* The shell's I76SHELL.DLL (image base 0x10000000), the same way */
#define SHELL_PATH "I76SHELL.DLL"
#define SHELL_SIZE 471552u
#define SHELL_CRC32 0xeee0a66du
bool dll_init(char *err, size_t errcap);
bool dll_loaded(void);
const uint8_t *dll_data(uint32_t va, size_t len);
uint32_t dll_u32(uint32_t va);
const char *dll_str(uint32_t va);

uint32_t crc32(const void *data, size_t len);
