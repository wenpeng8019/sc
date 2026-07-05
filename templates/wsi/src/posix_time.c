
#include "internal.h"

#if defined(GLFW_BUILD_POSIX_TIMER)

#include <unistd.h>
#include <sys/time.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void impl_platform_init_timer(void)
{
    g_wsi.timer.posix.clock = CLOCK_REALTIME;
    g_wsi.timer.posix.frequency = 1000000000;

#if defined(_POSIX_MONOTONIC_CLOCK)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        g_wsi.timer.posix.clock = CLOCK_MONOTONIC;
#endif
}

uint64_t impl_platform_get_timer_value(void)
{
    struct timespec ts;
    clock_gettime(g_wsi.timer.posix.clock, &ts);
    return (uint64_t) ts.tv_sec * g_wsi.timer.posix.frequency + (uint64_t) ts.tv_nsec;
}

uint64_t impl_platform_get_timer_frequency(void)
{
    return g_wsi.timer.posix.frequency;
}

#endif // GLFW_BUILD_POSIX_TIMER

