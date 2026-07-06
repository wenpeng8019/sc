#include "../surface.h"

#include <stdlib.h>

struct sc_surface
{
    int platform;
    void* nativeDisplay;
    void* nativeWindow;
};

SURFACE_API sc_surface* sc_surface_create_from_native(int platform,
                                                      void* nativeDisplay,
                                                      void* nativeWindow)
{
    if (!nativeWindow)
        return NULL;

    if (platform == SC_SURFACE_PLATFORM_ANY)
        return NULL;

    sc_surface* surface = (sc_surface*) calloc(1, sizeof(sc_surface));
    if (!surface)
        return NULL;

    surface->platform = platform;
    surface->nativeDisplay = nativeDisplay;
    surface->nativeWindow = nativeWindow;
    return surface;
}

SURFACE_API void sc_surface_destroy(sc_surface* surface)
{
    free(surface);
}

SURFACE_API int sc_surface_get_platform(sc_surface* surface)
{
    return surface ? surface->platform : SC_SURFACE_PLATFORM_ANY;
}

SURFACE_API void* sc_surface_get_native_display(sc_surface* surface)
{
    return surface ? surface->nativeDisplay : NULL;
}

SURFACE_API void* sc_surface_get_native_window(sc_surface* surface)
{
    return surface ? surface->nativeWindow : NULL;
}
