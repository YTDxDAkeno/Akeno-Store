/* Akeno Gaming Hub - home-screen shortcut tile.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Akeno creates a small title folder per shortcut (PPSA90500-PPSA90599) holding this eboot,
 * forward.txt (target title and arguments) and akeno-launch.elf. Started from the home screen,
 * the tile writes its own title ID into the payload's slot, sends it to the console's ELF
 * loader on 127.0.0.1:9021 and closes; the payload starts the target once the tile is gone.
 * The design follows the forwarder tiles of PS5_RetroArch and MihawkOS (GPL-3.0-or-later).
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SLOT_MARKER "AKENO-LAUNCH-REQUESTER-SLOT-0001"

int sceSystemServiceHideSplashScreen(void);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);
int sceKernelUsleep(unsigned microseconds);

static char *read_file(const char *path, long *size)
{
    FILE *file = fopen(path, "rb");
    char *data = NULL;
    long length = -1;
    if (file && fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0 &&
        (data = malloc((size_t)length + 1)) && fread(data, 1, (size_t)length, file) != (size_t)length)
    {
        free(data);
        data = NULL;
    }
    if (file)
        fclose(file);
    if (data)
    {
        data[length] = '\0';
        *size = length;
    }
    return data;
}

static void leave(void)
{
    sceSystemServiceLoadExec("exit", NULL);
    for (;;)
        sceKernelUsleep(100000);
}

int main(void)
{
    FILE *log = fopen("/data/akeno/launch/tile.log", "w");
    long param_size = 0, payload_size = 0;
    char *param = read_file("/app0/sce_sys/param.json", &param_size);
    char *payload = read_file("/app0/akeno-launch.elf", &payload_size);
    char title[10] = {0};
    const char *at = param ? strstr(param, "\"titleId\"") : NULL;
    if (at && (at = strchr(at + 9, '"')))
        memcpy(title, at + 1, 9);
    char *slot = NULL;
    for (long i = 0; payload && i + (long)sizeof(SLOT_MARKER) <= payload_size; i++)
        if (memcmp(payload + i, SLOT_MARKER, sizeof(SLOT_MARKER) - 1) == 0)
        {
            slot = payload + i;
            break;
        }
    if (!slot || strlen(title) != 9)
    {
        if (log)
            fprintf(log, "tile: payload or title ID missing\n"), fclose(log);
        leave();
    }
    memset(slot, 0, sizeof(SLOT_MARKER) - 1);
    memcpy(slot, title, 9);
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(9021);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sock < 0 || connect(sock, (struct sockaddr *)&address, sizeof address) != 0)
    {
        if (log)
            fprintf(log, "tile %s: no ELF loader on port 9021\n", title), fclose(log);
        leave();
    }
    long sent = 0;
    while (sent < payload_size)
    {
        const ssize_t n = send(sock, payload + sent, (size_t)(payload_size - sent), 0);
        if (n <= 0)
            break;
        sent += n;
    }
    close(sock);
    if (log)
        fprintf(log, "tile %s: sent %ld of %ld payload bytes\n", title, sent, payload_size), fclose(log);
    leave();
    return 0;
}
