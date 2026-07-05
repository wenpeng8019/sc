
#include "internal.h"

#if defined(GLFW_BUILD_WIN32_THREAD)

#include <assert.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

bool impl_platform_create_tls(_GLFWtls* tls)
{
    assert(tls->win32.allocated == false);

    tls->win32.index = TlsAlloc();
    if (tls->win32.index == TLS_OUT_OF_INDEXES)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_ERROR, "Win32: Failed to allocate TLS index");
        return false;
    }

    tls->win32.allocated = true;
    return true;
}

void impl_platform_destroy_tls(_GLFWtls* tls)
{
    if (tls->win32.allocated)
        TlsFree(tls->win32.index);
    memset(tls, 0, sizeof(_GLFWtls));
}

void* impl_platform_get_tls(_GLFWtls* tls)
{
    assert(tls->win32.allocated == true);
    return TlsGetValue(tls->win32.index);
}

void impl_platform_set_tls(_GLFWtls* tls, void* value)
{
    assert(tls->win32.allocated == true);
    TlsSetValue(tls->win32.index, value);
}

bool impl_platform_create_mutex(_GLFWmutex* mutex)
{
    assert(mutex->win32.allocated == false);
    InitializeCriticalSection(&mutex->win32.section);
    return mutex->win32.allocated = true;
}

void impl_platform_destroy_mutex(_GLFWmutex* mutex)
{
    if (mutex->win32.allocated)
        DeleteCriticalSection(&mutex->win32.section);
    memset(mutex, 0, sizeof(_GLFWmutex));
}

void impl_platform_lock_mutex(_GLFWmutex* mutex)
{
    assert(mutex->win32.allocated == true);
    EnterCriticalSection(&mutex->win32.section);
}

void impl_platform_unlock_mutex(_GLFWmutex* mutex)
{
    assert(mutex->win32.allocated == true);
    LeaveCriticalSection(&mutex->win32.section);
}

#endif // GLFW_BUILD_WIN32_THREAD

