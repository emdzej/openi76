/* Bwd2_Walk 0x483ec0: reading a BWD2 file's chunks (docs/formats/bwd2.md) against a table of what may
   come, each entry {tag, handler, flags}, as the mission loader and the definition loaders do.

   Flags (the original's, by bit):
     0x002  of the table's FIRST entry: handlers get the payload (past the 8-byte header), else the chunk
     0x004  in sequence: if the entry's chunk is missing, its handler is called with the chunk that is
            there (the release build's message is a no-op)
     0x008  a message when it is missing or its handler fails (a no-op)
     0x010  missing or failed: stop, returning the chunk
     0x020  missing or failed: stop, the walk fails
     0x040  mandatory: the walk fails if it was never seen
     0x080  seen (set by the walk, and never cleared: a later file's check passes)
     0x100  a count chunk: its handler gets a counter
     0x200  starts a repeat (the entries after it repeat until another chunk comes)
     0x400  repeats itself
     0x800  the handler gets the walk's context argument
     0x1000 the handler gets the entry itself
     0x2000 the handler gets the walk's name argument (the file's name)
   A handler returns nonzero on success. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*ChunkFn)(const uint8_t *chunk, void *arg, const uint8_t *end);
typedef struct {
    char tag[5];   /* 4 characters; the 5th is the original's 0 after them ("EXIT\0" is compared) */
    ChunkFn fn;
    uint32_t flags;
} ChunkDesc;

/* Walks from chunk p: in sequence (seq) through the n entries, or (not seq) matching each chunk's tag
   anywhere in the table until an EXIT chunk. Returns the chunk after the last one read, or NULL if the
   walk failed. end (may be NULL) bounds the walk. */
const uint8_t *Bwd2_Walk(const uint8_t *p, ChunkDesc *t, int n, void *ctx, const char *name, bool seq,
                         const uint8_t *end);
/* Bwd2_Read 0x484300: a whole file by name: "null..." succeeds with nothing read; the BWD2 and REV header
   (0x484390), then the table in sequence. */
bool Bwd2_Read(const char *name, ChunkDesc *t, int n, void *ctx);
/* True if the table's tags and flags are the exe's at va (tests: the port's tables are the original's). */
bool Chunks_Check(const ChunkDesc *t, int n, uint32_t va);
/* Rev_Check 0x484680: the REV and xREV chunks' handler (arg: the file's name) */
int Rev_Check(const uint8_t *c, void *arg, const uint8_t *end);
