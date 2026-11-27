#include "catalog.h"

#include <string.h>

static int read_line(FILE *stream, char *buffer, size_t size)
{
    size_t length = 0;
    int byte;

    while ((byte = fgetc(stream)) != EOF && byte != '\n') {
        if (byte == 0 || length == size - 1) return -1;
        buffer[length++] = (char)byte;
    }
    if (ferror(stream)) return -1;
    if (byte == EOF && length == 0) return 0;
    if (length && buffer[length - 1] == '\r') length--;
    buffer[length] = '\0';
    return 1;
}

static int read_game(char *line, Game *game)
{
    char *fields[] = {game->id, game->title, game->source, game->version};
    const size_t limits[] = {sizeof game->id, sizeof game->title, sizeof game->source, sizeof game->version};

    for (size_t field = 0; field < 4; field++) {
        char *end = strchr(line, '\t');
        if ((end != NULL) != (field < 3)) return -1;
        size_t bytes = end ? (size_t)(end - line) : strlen(line);
        if ((!bytes && field < 3) || bytes >= limits[field]) return -1;
        for (size_t index = 0; index < bytes; index++) {
            unsigned char byte = (unsigned char)line[index];
            if (byte < 32 || byte == 127) return -1;
        }
        memcpy(fields[field], line, bytes);
        fields[field][bytes] = '\0';
        if (end) line = end + 1;
    }
    return 0;
}

int catalog_read(FILE *stream, Catalog *catalog, char *error, size_t error_length)
{
    char line[600];
    int status;
    catalog->count = 0;
    while ((status = read_line(stream, line, sizeof line)) > 0) {
        if (catalog->count == CATALOG_LIMIT || read_game(line, &catalog->games[catalog->count]) != 0) break;
        catalog->count++;
    }
    if (status == 0) return 0;
    snprintf(error, error_length, "Ungueltiger oder zu grosser Katalog in Zeile %zu.", catalog->count + 1);
    catalog->count = 0;
    return -1;
}

int catalog_load(const char *path, Catalog *catalog, char *error, size_t error_length)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_length, "Katalogdatei konnte nicht geoeffnet werden: %s", path);
        return -1;
    }
    int result = catalog_read(file, catalog, error, error_length);
    fclose(file);
    return result;
}

int catalog_self_test(void)
{
    static Catalog catalog;
    char error[128];
    FILE *file = tmpfile();
    if (!file) return -1;
    fputs("signal\tSignal Drift\tAkeno\t1.0.0\r\nitem\tSecond\tHomebrew\t", file);
    rewind(file);
    int result = catalog_read(file, &catalog, error, sizeof error);
    fclose(file);
    if (result != 0 || catalog.count != 2 || strcmp(catalog.games[0].title, "Signal Drift")
        || strcmp(catalog.games[0].version, "1.0.0") || catalog.games[1].version[0]) return -1;

    file = tmpfile();
    if (!file) return -1;
    fputs("bad\tMissing version column\tAkeno\n", file);
    rewind(file);
    result = catalog_read(file, &catalog, error, sizeof error);
    fclose(file);
    return result != 0 && catalog.count == 0 ? 0 : -1;
}
