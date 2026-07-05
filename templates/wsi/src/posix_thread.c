
#include "internal.h"

#if defined(GLFW_BUILD_POSIX_THREAD)

#include <assert.h>
#include <string.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool impl_platform_create_tls(_GLFWtls* tls)
{
    assert(tls->posix.allocated == false);

    if (pthread_key_create(&tls->posix.key, NULL) != 0)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR,
                        "POSIX: Failed to create context TLS");
        return false;
    }

    tls->posix.allocated = true;
    return true;
}

void impl_platform_destroy_tls(_GLFWtls* tls)
{
    if (tls->posix.allocated)
        pthread_key_delete(tls->posix.key);
    memset(tls, 0, sizeof(_GLFWtls));
}

void* impl_platform_get_tls(_GLFWtls* tls)
{
    assert(tls->posix.allocated == true);
    return pthread_getspecific(tls->posix.key);
}

void impl_platform_set_tls(_GLFWtls* tls, void* value)
{
    assert(tls->posix.allocated == true);
    pthread_setspecific(tls->posix.key, value);
}

bool impl_platform_create_mutex(_GLFWmutex* mutex)
{
    assert(mutex->posix.allocated == false);

    if (pthread_mutex_init(&mutex->posix.handle, NULL) != 0)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "POSIX: Failed to create mutex");
        return false;
    }

    return mutex->posix.allocated = true;
}

void impl_platform_destroy_mutex(_GLFWmutex* mutex)
{
    if (mutex->posix.allocated)
        pthread_mutex_destroy(&mutex->posix.handle);
    memset(mutex, 0, sizeof(_GLFWmutex));
}

void impl_platform_lock_mutex(_GLFWmutex* mutex)
{
    assert(mutex->posix.allocated == true);
    pthread_mutex_lock(&mutex->posix.handle);
}

void impl_platform_unlock_mutex(_GLFWmutex* mutex)
{
    assert(mutex->posix.allocated == true);
    pthread_mutex_unlock(&mutex->posix.handle);
}

#endif // GLFW_BUILD_POSIX_THREAD

