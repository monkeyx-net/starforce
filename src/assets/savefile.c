#include "assets/savefile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

static int mkdir_p(const char *dir)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(tmp, 0755) && errno != EEXIST ? -1 : 0;
}

static const char *save_dir(char *buf, size_t len)
{
    const char *env = getenv("STARFORCE_SAVE_DIR");
    if (env && *env) return env;
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        snprintf(buf, len, "%s/starforce", xdg);
        return buf;
    }
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        snprintf(buf, len, "%s/starforce", appdata);
        return buf;
    }
#endif
    const char *home = getenv("HOME");
    snprintf(buf, len, "%s/.local/share/starforce", home && *home ? home : ".");
    return buf;
}

const char *save_path(const char *name)
{
    static char path[1200];
    char dir[1024];
    snprintf(path, sizeof path, "%s/%s", save_dir(dir, sizeof dir), name);
    return path;
}

int save_load(const char *name, void *buf, size_t len)
{
    FILE *f = fopen(save_path(name), "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, len, f);
    int extra = fgetc(f) != EOF;
    fclose(f);
    return got == len && !extra ? 0 : -1;
}

int save_store(const char *name, const void *buf, size_t len)
{
    char dir[1024], tmp[1220];
    if (mkdir_p(save_dir(dir, sizeof dir))) return -1;
    const char *p = save_path(name);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int bad = fwrite(buf, 1, len, f) != len;
    bad |= fclose(f) != 0;
#ifdef _WIN32
    if (!bad) remove(p); /* Windows rename() does not replace */
#endif
    if (bad || rename(tmp, p)) {
        remove(tmp);
        return -1;
    }
    return 0;
}
