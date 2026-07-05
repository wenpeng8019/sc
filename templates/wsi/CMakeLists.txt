cmake_minimum_required(VERSION 3.14)
project(wsi VERSION 3.5.0 LANGUAGES C)

# ============================================================
# sc win — 跨平台窗口库 (derived from GLFW 3.5)
#
# 平台宏（通过 -D 手动指定，未指定时自动检测宿主平台）：
#   -DWSI_COCOA      macOS Cocoa
#   -DWSI_WIN32      Windows Win32
#   -DWSI_X11        Linux X11
#   -DWSI_WAYLAND    Linux Wayland
# ============================================================

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---- 平台自动检测 ----
if(NOT _GLFW_PLATFORM)
    if(APPLE)
        set(_GLFW_PLATFORM COCOA)
    elseif(WIN32)
        set(_GLFW_PLATFORM WIN32)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_GLFW_PLATFORM X11)
    else()
        set(_GLFW_PLATFORM NULL)
    endif()
endif()

message(STATUS "wsi platform: ${_GLFW_PLATFORM}")

# ---- 共享源文件 ----
set(SHARED_SRCS
    ${SRC_DIR}/init.c
    ${SRC_DIR}/input.c
    ${SRC_DIR}/monitor.c
    ${SRC_DIR}/window.c
    ${SRC_DIR}/platform.c
)

set(PLAT_SRCS
        ${SRC_DIR}/cocoa_init.m
        ${SRC_DIR}/cocoa_monitor.m
        ${SRC_DIR}/cocoa_window.m
        ${SRC_DIR}/macos_time.c

        ${SRC_DIR}/win32_init.c
        ${SRC_DIR}/win32_monitor.c
        ${SRC_DIR}/win32_window.c

        ${SRC_DIR}/x11_init.c
        ${SRC_DIR}/x11_monitor.c
        ${SRC_DIR}/x11_window.c
        ${SRC_DIR}/xkb_unicode.c

        ${SRC_DIR}/wl_init.c
        ${SRC_DIR}/wl_monitor.c
        ${SRC_DIR}/wl_window.c
        ${SRC_DIR}/xkb_unicode.c

        ${SRC_DIR}/posix_poll.c
)
set(THREAD_SRCS
        ${SRC_DIR}/posix_thread.c
        ${SRC_DIR}/win32_thread.c
)
set(TIME_SRCS
        ${SRC_DIR}/posix_time.c
        ${SRC_DIR}/win32_time.c
)
set(POLL_SRCS)

set(PLAT_DEFINE  WSI_COCOA WSI_WIN32 WSI_X11 WSI_WAYLAND)

# ---- null 存根 ----
set(NULL_SRCS
    ${SRC_DIR}/null_init.c
    ${SRC_DIR}/null_monitor.c
    ${SRC_DIR}/null_window.c
)

# ---- 库目标 ----
add_library(wsi STATIC
    ${SHARED_SRCS}
    ${PLAT_SRCS}
    ${THREAD_SRCS}
    ${TIME_SRCS}
    ${POLL_SRCS}
    ${NULL_SRCS}
)

target_include_directories(wsi PRIVATE ${SRC_DIR})
target_include_directories(wsi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(wsi PRIVATE ${PLAT_DEFINE} WSI_BUILD_DLL)

# ---- 链接 ----
if(_GLFW_PLATFORM STREQUAL "COCOA")
    target_link_libraries(wsi PUBLIC
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework Carbon"
    )
elseif(_GLFW_PLATFORM STREQUAL "X11")
    target_link_libraries(wsi PUBLIC ${X11_LIBRARIES} Xrandr Xinerama Xcursor Xi)
elseif(_GLFW_PLATFORM STREQUAL "WIN32")
    target_link_libraries(wsi PUBLIC gdi32 user32)
elseif(_GLFW_PLATFORM STREQUAL "WAYLAND")
    find_package(Wayland REQUIRED)
    target_link_libraries(wsi PUBLIC wayland-client wayland-egl)
endif()

# ---- 安装 ----
install(TARGETS wsi ARCHIVE DESTINATION lib)
install(FILES win.h DESTINATION include)
