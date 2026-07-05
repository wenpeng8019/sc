
#include "internal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>


// NOTE: The global variables below comprise all mutable global data in GLFW
//       Any other mutable global variable is a bug

// This contains all mutable state shared between compilation units of GLFW
//
library_st g_wsi = { false };

// These are outside of g_wsi so they can be used before initialization and
// after termination without special handling when g_wsi is cleared to zero
//
static error_st _glfwMainThreadError;
static sc_error_cb _glfwErrorCallback;
static sc_allocator_cb _glfwInitAllocator;
static init_config_st _glfwInitHints =
{
    .hatButtons = true,
    .platformID = SC_PLATFORM_ANY,
    .ns =
    {
        .menubar = true,
        .chdir = true
    },
    .x11 =
    {
    },
    .wl =
    {
        .libdecorMode = SC_WAYLAND_PREFER_LIBDECOR
    },
};

// The allocation function used when no custom allocator is set
//
static void* defaultAllocate(size_t size, void* user)
{
    return malloc(size);
}

// The deallocation function used when no custom allocator is set
//
static void defaultDeallocate(void* block, void* user)
{
    free(block);
}

// The reallocation function used when no custom allocator is set
//
static void* defaultReallocate(void* block, size_t size, void* user)
{
    return realloc(block, size);
}

// Terminate the library
//
static void terminate(void)
{
    int i;

    memset(&g_wsi.callbacks, 0, sizeof(g_wsi.callbacks));

    while (g_wsi.windowListHead)
        sc_wsi_win_destroy((sc_window*) g_wsi.windowListHead);

    while (g_wsi.cursorListHead)
        sc_wsi_cursor_destroy((sc_cursor*) g_wsi.cursorListHead);

    for (i = 0;  i < g_wsi.monitorCount;  i++)
    {
        monitor_st* monitor = g_wsi.monitors[i];
        if (monitor->originalRamp.size)
            g_wsi.platform.setGammaRamp(monitor, &monitor->originalRamp);
        wsi_free_monitor(monitor);
    }

    wsi_free(g_wsi.monitors);
    g_wsi.monitors = NULL;
    g_wsi.monitorCount = 0;


    g_wsi.platform.terminate();

    g_wsi.initialized = false;

    while (g_wsi.errorListHead)
    {
        error_st* error = g_wsi.errorListHead;
        g_wsi.errorListHead = error->next;
        wsi_free(error);
    }

    impl_platform_destroy_tls(&g_wsi.errorSlot);
    impl_platform_destroy_mutex(&g_wsi.errorLock);

    memset(&g_wsi, 0, sizeof(g_wsi));
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Encode a Unicode code point to a UTF-8 stream
// Based on cutef8 by Jeff Bezanson (Public Domain)
//
size_t wsi_encode_urf8(char* s, uint32_t codepoint)
{
    size_t count = 0;

    if (codepoint < 0x80)
        s[count++] = (char) codepoint;
    else if (codepoint < 0x800)
    {
        s[count++] = (codepoint >> 6) | 0xc0;
        s[count++] = (codepoint & 0x3f) | 0x80;
    }
    else if (codepoint < 0x10000)
    {
        s[count++] = (codepoint >> 12) | 0xe0;
        s[count++] = ((codepoint >> 6) & 0x3f) | 0x80;
        s[count++] = (codepoint & 0x3f) | 0x80;
    }
    else if (codepoint < 0x110000)
    {
        s[count++] = (codepoint >> 18) | 0xf0;
        s[count++] = ((codepoint >> 12) & 0x3f) | 0x80;
        s[count++] = ((codepoint >> 6) & 0x3f) | 0x80;
        s[count++] = (codepoint & 0x3f) | 0x80;
    }

    return count;
}

// Splits and translates a text/uri-list into separate file paths
// NOTE: This function destroys the provided string
//
char** wsi_parse_url_list(char* text, int* count)
{
    const char* prefix = "file://";
    char** paths = NULL;
    char* line;

    *count = 0;

    while ((line = strtok(text, "\r\n")))
    {
        char* path;

        text = NULL;

        if (line[0] == '#')
            continue;

        if (strncmp(line, prefix, strlen(prefix)) == 0)
        {
            line += strlen(prefix);
            // TODO: Validate hostname
            while (*line != '/')
                line++;
        }

        (*count)++;

        path = wsi_calloc(strlen(line) + 1, 1);
        paths = wsi_realloc(paths, *count * sizeof(char*));
        paths[*count - 1] = path;

        while (*line)
        {
            if (line[0] == '%' && line[1] && line[2])
            {
                const char digits[3] = { line[1], line[2], '\0' };
                *path = (char) strtol(digits, NULL, 16);
                line += 2;
            }
            else
                *path = *line;

            path++;
            line++;
        }
    }

    return paths;
}

char* wsi_strdup(const char* source)
{
    const size_t length = strlen(source);
    char* result = wsi_calloc(length + 1, 1);
    strcpy(result, source);
    return result;
}

int wsi_min(int a, int b)
{
    return a < b ? a : b;
}

int wsi_max(int a, int b)
{
    return a > b ? a : b;
}

void* wsi_calloc(size_t count, size_t size)
{
    if (count && size)
    {
        void* block;

        if (count > SIZE_MAX / size)
        {
            impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Allocation size overflow");
            return NULL;
        }

        block = g_wsi.allocator.allocate(count * size, g_wsi.allocator.user);
        if (block)
            return memset(block, 0, count * size);
        else
        {
            impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY, NULL);
            return NULL;
        }
    }
    else
        return NULL;
}

void* wsi_realloc(void* block, size_t size)
{
    if (block && size)
    {
        void* resized = g_wsi.allocator.reallocate(block, size, g_wsi.allocator.user);
        if (resized)
            return resized;
        else
        {
            impl_on_error(SC_WSI_ERR_OUT_OF_MEMORY, NULL);
            return NULL;
        }
    }
    else if (block)
    {
        wsi_free(block);
        return NULL;
    }
    else
        return wsi_calloc(1, size);
}

void wsi_free(void* block)
{
    if (block)
        g_wsi.allocator.deallocate(block, g_wsi.allocator.user);
}


//////////////////////////////////////////////////////////////////////////
//////                         GLFW event API                       //////
//////////////////////////////////////////////////////////////////////////

// Notifies shared code of an error
//
void impl_on_error(int code, const char* format, ...)
{
    error_st* error;
    char description[_SC_MESSAGE_SIZE];

    if (format)
    {
        va_list vl;

        va_start(vl, format);
        vsnprintf(description, sizeof(description), format, vl);
        va_end(vl);

        description[sizeof(description) - 1] = '\0';
    }
    else
    {
        if (code == SC_WSI_ERR_NOT_INITIALIZED)
            strcpy(description, "The GLFW library is not initialized");
        else if (code == SC_WSI_ERR_NO_CURRENT_CONTEXT)
            strcpy(description, "There is no current context");
        else if (code == SC_WSI_ERR_INVALID_ENUM)
            strcpy(description, "Invalid argument for enum parameter");
        else if (code == SC_WSI_ERR_INVALID_VALUE)
            strcpy(description, "Invalid value for parameter");
        else if (code == SC_WSI_ERR_OUT_OF_MEMORY)
            strcpy(description, "Out of memory");
        else if (code == SC_WSI_ERR_API_UNAVAILABLE)
            strcpy(description, "The requested API is unavailable");
        else if (code == SC_WSI_ERR_VERSION_UNAVAILABLE)
            strcpy(description, "The requested API version is unavailable");
        else if (code == SC_WSI_ERR_PLATFORM_ERROR)
            strcpy(description, "A platform-specific error occurred");
        else if (code == SC_WSI_ERR_FORMAT_UNAVAILABLE)
            strcpy(description, "The requested format is unavailable");
        else if (code == SC_WSI_ERR_NO_WINDOW_CONTEXT)
            strcpy(description, "The specified window has no context");
        else if (code == SC_WSI_ERR_CURSOR_UNAVAILABLE)
            strcpy(description, "The specified cursor shape is unavailable");
        else if (code == SC_WSI_ERR_FEATURE_UNAVAILABLE)
            strcpy(description, "The requested feature cannot be implemented for this platform");
        else if (code == SC_WSI_ERR_FEATURE_UNIMPLEMENTED)
            strcpy(description, "The requested feature has not yet been implemented for this platform");
        else if (code == SC_WSI_ERR_PLATFORM_UNAVAILABLE)
            strcpy(description, "The requested platform is unavailable");
        else
            strcpy(description, "ERROR: UNKNOWN GLFW ERROR");
    }

    if (g_wsi.initialized)
    {
        error = impl_platform_get_tls(&g_wsi.errorSlot);
        if (!error)
        {
            error = wsi_calloc(1, sizeof(error_st));
            impl_platform_set_tls(&g_wsi.errorSlot, error);
            impl_platform_lock_mutex(&g_wsi.errorLock);
            error->next = g_wsi.errorListHead;
            g_wsi.errorListHead = error;
            impl_platform_unlock_mutex(&g_wsi.errorLock);
        }
    }
    else
        error = &_glfwMainThreadError;

    error->code = code;
    strcpy(error->description, description);

    if (_glfwErrorCallback)
        _glfwErrorCallback(code, description);
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW public API                       //////
//////////////////////////////////////////////////////////////////////////

WSI_API int sc_wsi_init(void)
{
    if (g_wsi.initialized)
        return true;

    memset(&g_wsi, 0, sizeof(g_wsi));
    g_wsi.hints.init = _glfwInitHints;

    g_wsi.allocator = _glfwInitAllocator;
    if (!g_wsi.allocator.allocate)
    {
        g_wsi.allocator.allocate   = defaultAllocate;
        g_wsi.allocator.reallocate = defaultReallocate;
        g_wsi.allocator.deallocate = defaultDeallocate;
    }

    if (!wsi_select_platform(g_wsi.hints.init.platformID, &g_wsi.platform))
        return false;

    if (!g_wsi.platform.init())
    {
        terminate();
        return false;
    }

    if (!impl_platform_create_mutex(&g_wsi.errorLock) ||
        !impl_platform_create_tls(&g_wsi.errorSlot))
    {
        terminate();
        return false;
    }

    impl_platform_set_tls(&g_wsi.errorSlot, &_glfwMainThreadError);


    impl_platform_init_timer();
    g_wsi.timer.offset = impl_platform_get_timer_value();

    g_wsi.initialized = true;

    sc_wsi_default_window_hints();
    return true;
}

WSI_API void sc_wsi_terminate(void)
{
    if (!g_wsi.initialized)
        return;

    terminate();
}

WSI_API void sc_wsi_init_hint(int hint, int value)
{
    switch (hint)
    {
        case SC_ANGLE_PLATFORM_TYPE:
            _glfwInitHints.angleType = value;
            return;
        case SC_PLATFORM:
            _glfwInitHints.platformID = value;
            return;
        case SC_COCOA_CHDIR_RESOURCES:
            _glfwInitHints.ns.chdir = value;
            return;
        case SC_COCOA_MENUBAR:
            _glfwInitHints.ns.menubar = value;
            return;
        case SC_WAYLAND_LIBDECOR:
            _glfwInitHints.wl.libdecorMode = value;
            return;
    }

    impl_on_error(SC_WSI_ERR_INVALID_ENUM,
                    "Invalid init hint 0x%08X", hint);
}

WSI_API void sc_wsi_init_allocator(const sc_allocator_cb* allocator)
{
    if (allocator)
    {
        if (allocator->allocate && allocator->reallocate && allocator->deallocate)
            _glfwInitAllocator = *allocator;
        else
            impl_on_error(SC_WSI_ERR_INVALID_VALUE, "Missing function in allocator");
    }
    else
        memset(&_glfwInitAllocator, 0, sizeof(sc_allocator_cb));
}

WSI_API void sc_wsi_get_version(int* major, int* minor, int* rev)
{
    if (major != NULL)
        *major = WSI_VERSION_MAJOR;
    if (minor != NULL)
        *minor = WSI_VERSION_MINOR;
    if (rev != NULL)
        *rev = WSI_VERSION_REVISION;
}

WSI_API int sc_wsi_get_error(const char** description)
{
    error_st* error;
    int code = SC_WSI_ERR_NONE;

    if (description)
        *description = NULL;

    if (g_wsi.initialized)
        error = impl_platform_get_tls(&g_wsi.errorSlot);
    else
        error = &_glfwMainThreadError;

    if (error)
    {
        code = error->code;
        error->code = SC_WSI_ERR_NONE;
        if (description && code)
            *description = error->description;
    }

    return code;
}

WSI_API sc_error_cb sc_wsi_set_error_callback(sc_error_cb cbfun)
{
    sc_error_cb t = _glfwErrorCallback;
    _glfwErrorCallback = cbfun;
    return t;
}

