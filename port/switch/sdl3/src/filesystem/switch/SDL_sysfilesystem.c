/* SDL3's file system functions for the Nintendo Switch (homebrew, libnx): part of Tenkaichi3Decomp's Switch port
   (docs/port/switch_plan.md), under that project's licence, NOT part of SDL. port/switch/sdl3/build.py copies it into
   SDL's source tree to build SDL for the port. SDL does not accept code written with AI tools (its CLAUDE.md), and
   this file was: it is not meant to be offered to SDL. Modelled on SDL's filesystem/n3ds/SDL_sysfilesystem.c. */
#include "SDL_internal.h"

#include "../SDL_sysfilesystem.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

extern int __system_argc;
extern char **__system_argv;

/* The folder of the program, from the path its loader started it with (argv[0], "sdmc:/.../program"), with a
   slash at the end; "sdmc:/" when there is none. */
char *SDL_SYS_GetBasePath(void)
{
    const char *exe = __system_argc > 0 ? __system_argv[0] : NULL;
    const char *slash = exe != NULL ? SDL_strrchr(exe, '/') : NULL;
    char *path;

    if (slash == NULL) {
        return SDL_strdup("sdmc:/");
    }
    path = (char *)SDL_malloc((size_t)(slash - exe) + 2);
    if (path == NULL) {
        return NULL;
    }
    SDL_memcpy(path, exe, (size_t)(slash - exe) + 1);
    path[slash - exe + 1] = '\0';
    return path;
}

char *SDL_SYS_GetExeName(void)
{
    const char *exe = __system_argc > 0 ? __system_argv[0] : NULL;
    const char *slash = exe != NULL ? SDL_strrchr(exe, '/') : NULL;

    return exe != NULL ? SDL_strdup(slash != NULL ? slash + 1 : exe) : NULL;
}

/* sdmc:/switch/<app>/, made when missing */
char *SDL_SYS_GetPrefPath(const char *org, const char *app)
{
    char *path;

    (void)org;
    if (SDL_asprintf(&path, "sdmc:/switch/%s/", app) < 0) {
        return NULL;
    }
    if (mkdir(path, 0777) == -1 && errno != EEXIST) {
        SDL_SetError("Failed to create '%s' (%s)", path, strerror(errno));
        SDL_free(path);
        return NULL;
    }
    return path;
}

char *SDL_SYS_GetUserFolder(SDL_Folder folder)
{
    (void)folder;
    SDL_Unsupported();
    return NULL;
}
