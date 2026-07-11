
#ifndef wsi_native_h_
#define wsi_native_h_

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************
 * Doxygen documentation
 *************************************************************************/

/*! @file glfw3native.h
 *  @brief The header of the native access functions.
 *
 *  This is the header file of the native access functions.  See @ref native for
 *  more information.
 */
/*! @defgroup native Native access
 *  @brief Functions related to accessing native handles.
 *
 *  **By using the native access functions you assert that you know what you're
 *  doing and how to fix problems caused by using them.  If you don't, you
 *  shouldn't be using them.**
 *
 *  Before the inclusion of @ref glfw3native.h, you may define zero or more
 *  window system API macro and zero or more context creation API macros.
 *
 *  The chosen backends must match those the library was compiled for.  Failure
 *  to do this will cause a link-time error.
 *
 *  The available window API macros are:
 *  * `WSI_EXPOSE_NATIVE_WIN32`
 *  * `WSI_EXPOSE_NATIVE_COCOA`
 *  * `WSI_EXPOSE_NATIVE_X11`
 *  * `WSI_EXPOSE_NATIVE_WAYLAND`
 *
 *
 *  These macros select which of the native access functions that are declared
 *  and which platform-specific headers to include.  It is then up your (by
 *  definition platform-specific) code to handle which of these should be
 *  defined.
 *
 *  If you do not want the platform-specific headers to be included, define
 *  `WSI_NATIVE_INCLUDE_NONE` before including the @ref glfw3native.h header.
 *
 *  @code
 *  #define WSI_EXPOSE_NATIVE_WIN32
 *  #define WSI_NATIVE_INCLUDE_NONE
 *  #include <wsi_native.h>
 *  @endcode
 */


/*************************************************************************
 * System headers and types
 *************************************************************************/

#if !defined(WSI_NATIVE_INCLUDE_NONE)

 #if defined(WSI_EXPOSE_NATIVE_WIN32)
  /* This is a workaround for the fact that glfw3.h needs to export APIENTRY (for
   * example to allow applications to correctly declare a GL_KHR_debug callback)
   * but windows.h assumes no one will define APIENTRY before it does
   */
  #if defined(WSI_APIENTRY_DEFINED)
   #undef APIENTRY
   #undef WSI_APIENTRY_DEFINED
  #endif
  #include <windows.h>
 #endif

 #if defined(WSI_EXPOSE_NATIVE_COCOA)
  #if defined(__OBJC__)
   #import <Cocoa/Cocoa.h>
  #else
   #include <ApplicationServices/ApplicationServices.h>
   #include <objc/objc.h>
  #endif
 #endif

 #if defined(WSI_EXPOSE_NATIVE_X11)
  #include <X11/Xlib.h>
  #include <X11/extensions/Xrandr.h>
 #endif

 #if defined(WSI_EXPOSE_NATIVE_WAYLAND)
  #include <wayland-client.h>
 #endif

#endif /*WSI_NATIVE_INCLUDE_NONE*/


/*************************************************************************
 * Functions
 *************************************************************************/

#if defined(WSI_EXPOSE_NATIVE_WIN32)
/*! @brief Returns the adapter device name of the specified monitor.
 *
 *  @return The UTF-8 encoded adapter device name (for example `\\.\DISPLAY1`)
 *  of the specified monitor, or `NULL` if an [error](@ref error_handling)
 *  occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.1.
 *
 *  @ingroup native
 */
WSI_API const char* wsi_get_win32_adapter(sc_monitor* monitor);

/*! @brief Returns the display device name of the specified monitor.
 *
 *  @return The UTF-8 encoded display device name (for example
 *  `\\.\DISPLAY1\Monitor0`) of the specified monitor, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.1.
 *
 *  @ingroup native
 */
WSI_API const char* wsi_get_win32_monitor(sc_monitor* monitor);

/*! @brief Returns the `HWND` of the specified window.
 *
 *  @return The `HWND` of the specified window, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @remark The `HDC` associated with the window can be queried with the
 *  [GetDC](https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdc)
 *  function.
 *  @code
 *  HDC dc = GetDC(wsi_get_win32_window(window));
 *  @endcode
 *  This DC is private and does not need to be released.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.0.
 *
 *  @ingroup native
 */
WSI_API HWND wsi_get_win32_window(sc_window* window);
#endif


#if defined(WSI_EXPOSE_NATIVE_COCOA)
/*! @brief Returns the `CGDirectDisplayID` of the specified monitor.
 *
 *  @return The `CGDirectDisplayID` of the specified monitor, or
 *  `kCGNullDirectDisplay` if an [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.1.
 *
 *  @ingroup native
 */
WSI_API CGDirectDisplayID wsi_get_cocoa_monitor(sc_monitor* monitor);

/*! @brief Returns the `NSWindow` of the specified window.
 *
 *  @return The `NSWindow` of the specified window, or `nil` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.0.
 *
 *  @ingroup native
 */
WSI_API id wsi_get_cocoa_window(sc_window* window);

/*! @brief Returns the `NSView` of the specified window.
 *
 *  @return The `NSView` of the specified window, or `nil` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.4.
 *
 *  @ingroup native
 */
WSI_API id wsi_get_cocoa_view(sc_window* window);
#endif


#if defined(WSI_EXPOSE_NATIVE_X11)
/*! @brief Returns the `Display` used by WSI.
 *
 *  @return The `Display` used by WSI, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.0.
 *
 *  @ingroup native
 */
WSI_API Display* wsi_get_x11_display(void);

/*! @brief Returns the `RRCrtc` of the specified monitor.
 *
 *  @return The `RRCrtc` of the specified monitor, or `None` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.1.
 *
 *  @ingroup native
 */
WSI_API RRCrtc wsi_get_x11_adapter(sc_monitor* monitor);

/*! @brief Returns the `RROutput` of the specified monitor.
 *
 *  @return The `RROutput` of the specified monitor, or `None` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.1.
 *
 *  @ingroup native
 */
WSI_API RROutput wsi_get_x11_monitor(sc_monitor* monitor);

/*! @brief Returns the `Window` of the specified window.
 *
 *  @return The `Window` of the specified window, or `None` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.0.
 *
 *  @ingroup native
 */
WSI_API Window wsi_get_x11_window(sc_window* window);

/*! @brief Sets the current primary selection to the specified string.
 *
 *  @param[in] string A UTF-8 encoded string.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE and @ref SC_WSI_ERR_PLATFORM_ERROR.
 *
 *  @pointer_lifetime The specified string is copied before this function
 *  returns.
 *
 *  @thread_safety This function must only be called from the main thread.
 *
 *  @sa @ref clipboard
 *  @sa wsi_get_x11_selection_string
 *  @sa glfwSetClipboardString
 *
 *  @since Added in version 3.3.
 *
 *  @ingroup native
 */
WSI_API void wsi_set_x11_selection_string(const char* string);

/*! @brief Returns the contents of the current primary selection as a string.
 *
 *  If the selection is empty or if its contents cannot be converted, `NULL`
 *  is returned and a @ref SC_WSI_ERR_FORMAT_UNAVAILABLE error is generated.
 *
 *  @return The contents of the selection as a UTF-8 encoded string, or `NULL`
 *  if an [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED, @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE and @ref SC_WSI_ERR_PLATFORM_ERROR.
 *
 *  @pointer_lifetime The returned string is allocated and freed by WSI. You
 *  should not free it yourself. It is valid until the next call to @ref
 *  wsi_get_x11_selection_string or @ref wsi_set_x11_selection_string, or until the
 *  library is terminated.
 *
 *  @thread_safety This function must only be called from the main thread.
 *
 *  @sa @ref clipboard
 *  @sa wsi_set_x11_selection_string
 *  @sa glfwGetClipboardString
 *
 *  @since Added in version 3.3.
 *
 *  @ingroup native
 */
WSI_API const char* wsi_get_x11_selection_string(void);
#endif


#if defined(WSI_EXPOSE_NATIVE_WAYLAND)
/*! @brief Returns the `struct wl_display*` used by WSI.
 *
 *  @return The `struct wl_display*` used by WSI, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.2.
 *
 *  @ingroup native
 */
WSI_API struct wl_display* wsi_get_wayland_display(void);

/*! @brief Returns the `struct wl_output*` of the specified monitor.
 *
 *  @return The `struct wl_output*` of the specified monitor, or `NULL` if an
 *  [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.2.
 *
 *  @ingroup native
 */
WSI_API struct wl_output* wsi_get_wayland_monitor(sc_monitor* monitor);

/*! @brief Returns the main `struct wl_surface*` of the specified window.
 *
 *  @return The main `struct wl_surface*` of the specified window, or `NULL` if
 *  an [error](@ref error_handling) occurred.
 *
 *  @errors Possible errors include @ref SC_WSI_ERR_NOT_INITIALIZED and @ref
 *  SC_WSI_ERR_PLATFORM_UNAVAILABLE.
 *
 *  @thread_safety This function may be called from any thread.  Access is not
 *  synchronized.
 *
 *  @since Added in version 3.2.
 *
 *  @ingroup native
 */
WSI_API struct wl_surface* wsi_get_wayland_window(sc_window* window);
#endif


#ifdef __cplusplus
}
#endif

#endif /* wsi_native_h_ */

