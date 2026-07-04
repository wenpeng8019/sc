
#include <pthread.h>

#define GLFW_POSIX_TLS_STATE    _GLFWtlsPOSIX   posix;
#define GLFW_POSIX_MUTEX_STATE  _GLFWmutexPOSIX posix;


// POSIX-specific thread local storage data
//
typedef struct _GLFWtlsPOSIX
{
    GLFWbool        allocated;
    pthread_key_t   key;
} _GLFWtlsPOSIX;

// POSIX-specific mutex data
//
typedef struct _GLFWmutexPOSIX
{
    GLFWbool        allocated;
    pthread_mutex_t handle;
} _GLFWmutexPOSIX;

