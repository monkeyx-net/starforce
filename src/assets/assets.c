#include "assets/assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int has_file(const char *dir, const char *rel)
{
    char path[1100];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    return stat(path, &st) == 0;
}

int assets_find(const char *argv0, const char *rel, char *out, size_t outlen)
{
    const char *env = getenv("STARFORCE_ASSETS");
    if (env && has_file(env, rel)) {
        snprintf(out, outlen, "%s", env);
        return 0;
    }
    if (argv0) {
        char exe_dir[900];
        snprintf(exe_dir, sizeof exe_dir, "%s", argv0);
        char *slash = strrchr(exe_dir, '/');
#ifdef _WIN32
        char *bslash = strrchr(exe_dir, '\\');
        if (bslash > slash) slash = bslash;
#endif
        if (slash) *slash = '\0';
        else snprintf(exe_dir, sizeof exe_dir, ".");
        snprintf(out, outlen, "%s/assets", exe_dir);
        if (has_file(out, rel)) return 0;
    }
#ifdef STARFORCE_SOURCE_DIR
    snprintf(out, outlen, "%s/assets", STARFORCE_SOURCE_DIR);
    if (has_file(out, rel)) return 0;
#endif
    out[0] = '\0';
    return -1;
}
