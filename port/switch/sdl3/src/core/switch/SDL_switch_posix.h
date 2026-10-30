/* For SDL3 on the Nintendo Switch (Tenkaichi3Decomp, port/switch/sdl3; not part of SDL): POSIX calls that SDL's
   pthread back end uses (thread/pthread/SDL_systhread.c) and that devkitA64's newlib does not declare or libnx does
   not have on this system: the scheduling calls for thread priorities, and pthread_sigmask, which SDL calls in every
   new thread (Horizon has no signals). Included in every SDL source by switch.cmake; SDL_switch_posix.c has them. */
#ifndef SDL_SWITCH_POSIX_H
#define SDL_SWITCH_POSIX_H
#include <pthread.h>
#include <sched.h>
#include <signal.h>

int pthread_getschedparam(pthread_t thread, int *policy, struct sched_param *param);
int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param);
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old);
#endif
