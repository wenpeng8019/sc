
#include "internal.h"

#if defined(GLFW_BUILD_POSIX_TIMER)

#include <unistd.h>
#include <sys/time.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwPlatformInitTimer(void)
{
    _glfw.timer.posix.clock = CLOCK_REALTIME;
    _glfw.timer.posix.frequency = 1000000000;

#if defined(_POSIX_MONOTONIC_CLOCK)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        _glfw.timer.posix.clock = CLOCK_MONOTONIC;
#endif
}

uint64_t _glfwPlatformGetTimerValue(void)
{
    struct timespec ts;
    clock_gettime(_glfw.timer.posix.clock, &ts);
    return (uint64_t) ts.tv_sec * _glfw.timer.posix.frequency + (uint64_t) ts.tv_nsec;
}

uint64_t _glfwPlatformGetTimerFrequency(void)
{
    return _glfw.timer.posix.frequency;
}

#endif // GLFW_BUILD_POSIX_TIMER

