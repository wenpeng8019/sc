
#include "internal.h"

#include <string.h>
#include <stdlib.h>

// These construct a string literal from individual numeric constants
#define _GLFW_CONCAT_VERSION(m, n, r) #m "." #n "." #r
#define _GLFW_MAKE_VERSION(m, n, r) _GLFW_CONCAT_VERSION(m, n, r)

//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

static const struct
{
    int ID;
    GLFWbool (*connect)(int,_GLFWplatform*);
} supportedPlatforms[] =
{
#if defined(_GLFW_WIN32)
    { SC_PLATFORM_WIN32, _glfwConnectWin32 },
#endif
#if defined(_GLFW_COCOA)
    { SC_PLATFORM_COCOA, _glfwConnectCocoa },
#endif
#if defined(_GLFW_WAYLAND)
    { SC_PLATFORM_WAYLAND, _glfwConnectWayland },
#endif
#if defined(_GLFW_X11)
    { SC_PLATFORM_X11, _glfwConnectX11 },
#endif
};

GLFWbool _glfwSelectPlatform(int desiredID, _GLFWplatform* platform)
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
        _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid platform ID 0x%08X", desiredID);
        return GLFW_FALSE;
    }

    // Only allow the Null platform if specifically requested
    if (desiredID == SC_PLATFORM_NULL)
        return _glfwConnectNull(desiredID, platform);
    else if (count == 0)
    {
        _glfwInputError(SC_WIN_ERR_PLATFORM_UNAVAILABLE, "This binary only supports the Null platform");
        return GLFW_FALSE;
    }

#if defined(_GLFW_WAYLAND) && defined(_GLFW_X11)
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
                return GLFW_TRUE;
        }

        _glfwInputError(SC_WIN_ERR_PLATFORM_UNAVAILABLE, "Failed to detect any supported platform");
    }
    else
    {
        for (i = 0;  i < count;  i++)
        {
            if (supportedPlatforms[i].ID == desiredID)
                return supportedPlatforms[i].connect(desiredID, platform);
        }

        _glfwInputError(SC_WIN_ERR_PLATFORM_UNAVAILABLE, "The requested platform is not supported");
    }

    return GLFW_FALSE;
}

//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI int sc_win_get_platform(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(0);
    return _glfw.platform.platformID;
}

GLFWAPI int sc_win_platform_supported(int platformID)
{
    const size_t count = sizeof(supportedPlatforms) / sizeof(supportedPlatforms[0]);
    size_t i;

    if (platformID != SC_PLATFORM_WIN32 &&
        platformID != SC_PLATFORM_COCOA &&
        platformID != SC_PLATFORM_WAYLAND &&
        platformID != SC_PLATFORM_X11 &&
        platformID != SC_PLATFORM_NULL)
    {
        _glfwInputError(SC_WIN_ERR_INVALID_ENUM, "Invalid platform ID 0x%08X", platformID);
        return GLFW_FALSE;
    }

    if (platformID == SC_PLATFORM_NULL)
        return GLFW_TRUE;

    for (i = 0;  i < count;  i++)
    {
        if (platformID == supportedPlatforms[i].ID)
            return GLFW_TRUE;
    }

    return GLFW_FALSE;
}

GLFWAPI const char* sc_win_get_version_string(void)
{
    return _GLFW_MAKE_VERSION(GLFW_VERSION_MAJOR,
                              GLFW_VERSION_MINOR,
                              GLFW_VERSION_REVISION)
#if defined(_GLFW_WIN32)
        " Win32 WGL"
#endif
#if defined(_GLFW_COCOA)
        " Cocoa NSGL"
#endif
#if defined(_GLFW_WAYLAND)
        " Wayland"
#endif
#if defined(_GLFW_X11)
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
#if defined(_GLFW_USE_HYBRID_HPG) || defined(_GLFW_USE_OPTIMUS_HPG)
        " hybrid-GPU"
#endif
#if defined(_POSIX_MONOTONIC_CLOCK)
        " monotonic"
#endif
#if defined(_GLFW_BUILD_DLL)
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

