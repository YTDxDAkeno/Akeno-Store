#ifndef AKENO_CATALOG_H
#define AKENO_CATALOG_H

#include <stddef.h>
#include <stdio.h>

enum { CATALOG_LIMIT = 1024 };
typedef struct {
    char id[96];
    char title[256];
    char source[96];
    char version[96];
} Game;

typedef struct {
    Game games[CATALOG_LIMIT];
    size_t count;
} Catalog;

/* Returns zero on success or a diagnostic on malformed, oversized, or unreadable input. */
int catalog_read(FILE *stream, Catalog *catalog, char *error, size_t error_length);
int catalog_load(const char *path, Catalog *catalog, char *error, size_t error_length);
int catalog_self_test(void);

#endif
