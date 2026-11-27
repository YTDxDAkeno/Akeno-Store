/* Akeno Gaming Hub - launch payload, run by the console's ELF loader (127.0.0.1:9021).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A PS5 title cannot start another title, so Akeno and its home-screen shortcut tiles send
 * this payload to the ELF loader with their own title ID written over the slot marker below
 * (the forwarder design of PS5_RetroArch and MihawkOS, GPL-3.0-or-later). The payload waits
 * until that title has closed, reads the request and starts the target title as the
 * foreground user with sceLncUtilLaunchApp, falling back to sceSystemServiceLaunchApp.
 *
 * Request files, one item per line:  AKENO-LAUNCH 1 / <target title ID> / <argument>...
 *   /data/homebrew/<requester>/forward.txt   (shortcut tiles)
 *   /data/akeno/launch/<requester>.txt       (Akeno itself)
 * Each step is logged to /data/akeno/launch/last.log.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SLOT_MARKER "AKENO-LAUNCH-REQUESTER-SLOT-0001"
#define MAX_ARGUMENTS 8
#define MAX_ARGUMENT 512

typedef struct
{
    uint32_t size;
    int user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint64_t check_flag;
} lnc_app_param_t;

int sceSystemServiceLaunchApp(const char *title_id, const char *argv[], lnc_app_param_t *param);
int sceLncUtilLaunchApp(const char *title_id, const char *argv[], lnc_app_param_t *param);
uint32_t sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t app_id, char *title_id);
int sceUserServiceInitialize(void *param);
int sceUserServiceGetForegroundUser(int *user_id);

/* The requester writes its title ID over this marker in the payload's bytes. */
__attribute__((used)) volatile char requester_slot[40] = SLOT_MARKER;

static int valid_title_id(const char *id)
{
    if (strlen(id) != 9)
        return 0;
    for (int i = 0; i < 9; i++)
        if (i < 4 ? !(id[i] >= 'A' && id[i] <= 'Z') : !(id[i] >= '0' && id[i] <= '9'))
            return 0;
    return 1;
}

static void running_title(char title[16])
{
    const uint32_t app = sceLncUtilGetAppIdOfRunningBigApp();
    memset(title, 0, 16);
    if (app != 0 && app != 0xffffffffu && sceLncUtilGetAppTitleId(app, title) != 0)
        memset(title, 0, 16);
    title[15] = '\0';
}

int main(void)
{
    char requester[16] = {0};
    for (int i = 0; i < 9; i++)
        requester[i] = requester_slot[i];
    FILE *log = fopen("/data/akeno/launch/last.log", "w");
    if (!valid_title_id(requester))
    {
        if (log)
            fprintf(log, "no requester in the slot; nothing launched\n"), fclose(log);
        return 1;
    }
    char path[160];
    snprintf(path, sizeof path, "/data/homebrew/%s/forward.txt", requester);
    FILE *input = fopen(path, "r");
    if (!input)
    {
        snprintf(path, sizeof path, "/data/akeno/launch/%s.txt", requester);
        input = fopen(path, "r");
    }
    static char lines[MAX_ARGUMENTS + 2][MAX_ARGUMENT];
    int count = 0;
    while (input && count < MAX_ARGUMENTS + 2 && fgets(lines[count], sizeof lines[count], input))
    {
        lines[count][strcspn(lines[count], "\r\n")] = '\0';
        count++;
    }
    if (input)
        fclose(input);
    if (count < 2 || strcmp(lines[0], "AKENO-LAUNCH 1") != 0 || !valid_title_id(lines[1]))
    {
        if (log)
            fprintf(log, "%s: no valid request in %s\n", requester, path), fclose(log);
        return 1;
    }
    const char *target = lines[1];
    const char *arguments[MAX_ARGUMENTS + 1];
    int argc = 0;
    for (int i = 2; i < count && argc < MAX_ARGUMENTS; i++)
        if (lines[i][0])
            arguments[argc++] = lines[i];
    arguments[argc] = NULL;

    sceUserServiceInitialize(NULL);
    int user = -1;
    const int foreground = sceUserServiceGetForegroundUser(&user);
    char title[16];
    int waited = 0;
    for (running_title(title); strcmp(title, requester) == 0 && waited < 60; waited++)
    {
        usleep(250000);
        running_title(title);
    }
    if (log)
        fprintf(log, "%s -> %s with %d argument(s); user %d (0x%08x); waited %d step(s), running '%s'\n", requester,
                target, argc, user, (unsigned)foreground, waited, title);
    lnc_app_param_t param = {sizeof(param), user, 0, 0, 0};
    int result = sceLncUtilLaunchApp(target, arguments, &param);
    if (log)
        fprintf(log, "sceLncUtilLaunchApp = 0x%08x\n", (unsigned)result);
    if (result < 0)
    {
        result = sceSystemServiceLaunchApp(target, arguments, &param);
        if (log)
            fprintf(log, "sceSystemServiceLaunchApp = 0x%08x\n", (unsigned)result);
    }
    if (log)
        fclose(log);
    return result < 0;
}
