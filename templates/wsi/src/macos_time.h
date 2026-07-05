
#define GLFW_MACOS_LIBRARY_TIMER_STATE _GLFWtimerMacOS macos;

// macOS-specific global timer data
//
typedef struct _GLFWtimerMacOS
{
    uint64_t        frequency;
} _GLFWtimerMacOS;

