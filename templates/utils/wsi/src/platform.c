
#include "internal.h"

#include <string.h>
#include <stdlib.h>

//////////////////////////////////////////////////////////////////////////
//////              平台模块动态加载（跨平台，各后端共用）          //////
//////////////////////////////////////////////////////////////////////////
// 供各后端（win32/x11/wayland）按名加载系统库并取符号；cocoa 后端不依赖，
// 但定义为平台无关，随 platform.c 一并编入，避免各后端各自实现一份。
#if defined(_WIN32)
 #include <windows.h>
void* impl_platform_load_module(const char* path)
{
    return LoadLibraryA(path);
}

void impl_platform_unload_module(void* module)
{
    FreeLibrary((HMODULE) module);
}

GLFWproc impl_platform_get_module_symbol(void* module, const char* name)
{
    return (GLFWproc) GetProcAddress((HMODULE) module, name);
}
#else
 #include <dlfcn.h>
void* impl_platform_load_module(const char* path)
{
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
}

void impl_platform_unload_module(void* module)
{
    dlclose(module);
}

GLFWproc impl_platform_get_module_symbol(void* module, const char* name)
{
    return (GLFWproc) dlsym(module, name);
}
#endif

// These construct a string literal from individual numeric constants
#define WSI_MAKE_VERSION(m, n, r) #m "." #n "." #r

//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

WSI_API const char* sc_wsi_get_version_string(void)
{
    return WSI_MAKE_VERSION(WSI_VERSION_MAJOR,
                              WSI_VERSION_MINOR,
                              WSI_VERSION_REVISION)
#if defined(WSI_WIN32)
        " Win32 WGL"
#endif
#if defined(WSI_COCOA)
        " Cocoa NSGL"
#endif
#if defined(WSI_WAYLAND)
        " Wayland"
#endif
#if defined(WSI_X11)
        " X11 GLX"
#endif
        " Null"
        " EGL"
        " OSMesa"
#if defined(__MINGW64_VERSION_MAJOR)
        " MinGW-w64"
#elif defined(_MSC_VER)
        " VisualC"
#endif
#if defined(WSI_USE_HYBRID_HPG) || defined(WSI_USE_OPTIMUS_HPG)
        " hybrid-GPU"
#endif
#if defined(_POSIX_MONOTONIC_CLOCK)
        " monotonic"
#endif
#if defined(WSI_SHARED)
#if defined(_WIN32)
        " DLL"
#elif defined(__APPLE__)
        " dynamic"
#else
        " shared"
#endif
#endif
        ;
}

static const struct
{
    int ID;
    bool (*connect)(int, platform_st*);
} supportedPlatforms[] =
{
#if defined(WSI_WIN32)
    { SC_PLATFORM_WIN32, win32_connect },
#endif
#if defined(WSI_COCOA)
    { SC_PLATFORM_COCOA, cocoa_connect },
#endif
#if defined(WSI_WAYLAND)
    { SC_PLATFORM_WAYLAND, wayland_connect },
#endif
#if defined(WSI_X11)
    { SC_PLATFORM_X11, x11_connect },
#endif
};

bool wsi_select_platform(int desiredID, platform_st* platform)
{
    const size_t count = sizeof(supportedPlatforms) / sizeof(supportedPlatforms[0]);
    size_t i;

    if (desiredID != SC_PLATFORM_ANY &&
        desiredID != SC_PLATFORM_WIN32 &&
        desiredID != SC_PLATFORM_COCOA &&
        desiredID != SC_PLATFORM_WAYLAND &&
        desiredID != SC_PLATFORM_X11 &&
        desiredID != SC_PLATFORM_NULL)
    {
        impl_on_error(SC_WSI_ERR_INVALID_ENUM, "Invalid platform ID 0x%08X", desiredID);
        return false;
    }

    // Only allow the Null platform if specifically requested
    if (desiredID == SC_PLATFORM_NULL)
        return null_connect(desiredID, platform);
    else if (count == 0)
    {
        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "This binary only supports the Null platform");
        return false;
    }

#if defined(WSI_WAYLAND) && defined(WSI_X11)
    if (desiredID == SC_PLATFORM_ANY)
    {
        const char* const session = getenv("XDG_SESSION_TYPE");
        if (session)
        {
            // Only follow XDG_SESSION_TYPE if it is set correctly and the
            // environment looks plausble; otherwise fall back to detection
            if (strcmp(session, "wayland") == 0 && getenv("WAYLAND_DISPLAY"))
                desiredID = SC_PLATFORM_WAYLAND;
            else if (strcmp(session, "x11") == 0 && getenv("DISPLAY"))
                desiredID = SC_PLATFORM_X11;
        }
    }
#endif

    if (desiredID == SC_PLATFORM_ANY)
    {
        // If there is exactly one platform available for auto-selection, let it emit the
        // error on failure as the platform-specific error description may be more helpful
        if (count == 1)
            return supportedPlatforms[0].connect(supportedPlatforms[0].ID, platform);

        for (i = 0;  i < count;  i++)
        {
            if (supportedPlatforms[i].connect(desiredID, platform))
                return true;
        }

        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "Failed to detect any supported platform");
    }
    else
    {
        for (i = 0;  i < count;  i++)
        {
            if (supportedPlatforms[i].ID == desiredID)
                return supportedPlatforms[i].connect(desiredID, platform);
        }

        impl_on_error(SC_WSI_ERR_PLATFORM_UNAVAILABLE, "The requested platform is not supported");
    }

    return false;
}

