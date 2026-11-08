/* PS5Term - DEVELOPMENT-ONLY direct title launch probe. GPL-3.0-or-later.
 * Calls Sony's in-process API ONCE. No ptrace, ShellUI RPC, retry, kernel
 * patch, helper replacement, or default console. Never part of the app.
 * Context layout follows ps5upload payload/src/register.c (phantomptr).
 */
#include <stdint.h>
#include <stddef.h>
struct launch_context
{
    uint32_t size, user, options;
    uint64_t crash_report;
    uint32_t check;
};
_Static_assert(sizeof(struct launch_context) == 32, "Sony launch context size");
_Static_assert(offsetof(struct launch_context, crash_report) == 16, "Sony launch context padding");
typedef int (*launch_fn)(const char *, const char *const *, const void *);
static int ps5term_launch_once(launch_fn launch, int user)
{
    if (!launch || user <= 0) return -1;
    const char *args[] = {NULL};
    struct launch_context context = {0};
    context.size = sizeof(context); context.user = (uint32_t)user;
    return launch("FAKE99260", args, &context);
}
#ifndef PS5TERM_LAUNCH_POLICY_ONLY
#include <dlfcn.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
static void record(const char *phase, int rc)
{
    mkdir("/data/ps5term-dev", 0700);
    FILE *file = fopen("/data/ps5term-dev/launch-probe.json", "w");
    if (!file) return;
    fprintf(file, "{\"schema\":1,\"title_id\":\"FAKE99260\",\"phase\":\"%s\",\"rc\":%d,\"rc_hex\":\"0x%08x\",\"fallback\":false,\"pid\":%d}\n",
            phase, rc, (unsigned)rc, (int)getpid());
    fclose(file);
}
int main(void)
{
    record("resolving", -1);
    void *system = dlopen("libSceSystemService.sprx", RTLD_LAZY);
    void *users = dlopen("libSceUserService.sprx", RTLD_LAZY);
    if (!system || !users) { record("module-unavailable", -1); return 1; }
    launch_fn launch = (launch_fn)dlsym(system, "sceSystemServiceLaunchApp");
    int (*initialize)(const void *) = dlsym(users, "sceUserServiceInitialize");
    int (*foreground)(int *) = dlsym(users, "sceUserServiceGetForegroundUser");
    if (!launch || !foreground) { record("symbol-unavailable", -1); return 1; }
    if (initialize) initialize(NULL);
    int user = -1;
    if (foreground(&user) != 0 || user <= 0) { record("no-foreground-user", -1); return 1; }
    record("calling-once", -1);
    int rc = ps5term_launch_once(launch, user);
    record(rc >= 0 ? "accepted-not-verified" : "rejected", rc);
    return rc < 0;
}
#endif
