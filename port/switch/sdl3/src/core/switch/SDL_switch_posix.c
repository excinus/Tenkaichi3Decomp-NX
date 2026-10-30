/* For SDL3 on the Nintendo Switch (Tenkaichi3Decomp, port/switch/sdl3; under that project's licence, not part of SDL
   and not meant to be offered to it: SDL does not accept code written with AI tools, which this was).

   The POSIX calls of SDL_switch_posix.h. libnx's threads have Horizon's priorities (svcSetThreadPriority),
   not POSIX's; these keep every thread at the priority it was created with and report success, so that
   SDL_SetCurrentThreadPriority does not fail. (The port asks for a low priority only for its texture-pack loader.) */
#include "SDL_switch_posix.h"
#include <string.h>

int pthread_getschedparam(pthread_t thread, int *policy, struct sched_param *param)
{
    (void)thread;
    *policy = SCHED_OTHER;
    memset(param, 0, sizeof(*param));
    return 0;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param)
{
    (void)thread;
    (void)policy;
    (void)param;
    return 0;
}

int sched_get_priority_min(int policy)
{
    (void)policy;
    return 0;
}

int sched_get_priority_max(int policy)
{
    (void)policy;
    return 0;
}

/* No signals on Horizon: nothing to block or unblock (the old mask is empty) */
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
    (void)how;
    (void)set;
    if (old != NULL) {
        sigemptyset(old);
    }
    return 0;
}
