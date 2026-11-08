/* The files the game writes into its install directory (the car between a trip's missions,
   addon\vehscn.vcf and .vsf; later the settings and the trip's bookmarks): over the data in the file
   layer (vfs_put), so the game reads them back by the same paths, and kept in the platform's user files
   (gasm storage) for the next run. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* fopen(path, "wb"), fwrite and fclose in one: false if it cannot be kept */
bool Files_Write(const char *path, const void *data, size_t size);
/* remove(path): the written file and its kept copy gone; false if there was none */
bool Files_Remove(const char *path);
/* the user files put back over the data (at start-up, after the data is mounted) */
void Files_Restore(void);
