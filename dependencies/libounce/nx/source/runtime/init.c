#include "types.h"
#include "runtime/env.h"
#include "runtime/hosversion.h"
#include "services/sm.h"
#include "services/fs.h"
#include "services/hid.h"
#include "services/time.h"
#include "services/applet.h"
#include "services/set.h"
#include "runtime/diag.h"
#include "runtime/devices/fs_dev.h"

void NX_NORETURN __nx_exit(Result rc, LoaderReturnFn retaddr);

void virtmemSetup(void);
void newlibSetup(void);
void argvSetup(void);
void __libnx_init_thread(void);
void __libnx_init_time(void);
bool __libnx_hosversion_is_set(void);
void __libnx_init_cwd(void);

extern u32 __nx_applet_type;

// Must be a multiple of 0x200000.
__attribute__((weak)) size_t __nx_heap_size = 0;

/// Override these with your own if you're using \ref__libnx_exception_handler. __nx_exception_stack is the stack-bottom. Update \ref __nx_exception_stack_size if you change this.
__attribute__((weak)) alignas(16) u8 __nx_exception_stack[0x400];
__attribute__((weak)) u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
/// By default exception handling will be aborted when the current process is being debugged. Set this to non-zero to disable that.
__attribute__((weak)) u32 __nx_exception_ignoredebug = 0;

/// Override this with your own func to handle exceptions. See \ref __nx_exception_stack. See here: https://switchbrew.org/wiki/SVC#Exception_handling
void __attribute__((weak)) __libnx_exception_handler(ThreadExceptionDump *ctx);

ThreadExceptionDump __nx_exceptiondump;

/*
  There are three ways of allocating heap:

    - Normal syscall:

    Allocates heap using |svcSetHeapSize|. The size is provided by a weak symbol
    called |__nx_heap_size|. With the default 0 value, the size is automatically
    determined with svcGetInfo. If running under a process where heap was already
    allocated with svcSetHeapSize, __nx_heap_size should be set manually.

    - Heap override:

    Uses existing heap segment as provided by the homebrew loader environment
    block. This happens automatically if such a setting is provided by the
    homebrew environment.

    - Custom override:

    A program can override the weak symbol |__libnx_initheap| to setup a
    different heap. In this case, the global variables |fake_heap_start|
    and |fake_heap_end| needs to be set appropriately.

    A custom override can be used to implement an "inner heap" located in the .bss
    segment of a process, for example.
 */

void __attribute__((weak)) __libnx_initheap(void)
{
    void*  addr;
    size_t size = 0;
    size_t mem_available = 0, mem_used = 0;

    if (envHasHeapOverride()) {
        addr = envGetHeapOverrideAddr();
        size = envGetHeapOverrideSize();
    }
    else {
        if (__nx_heap_size==0) {
            svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
            svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
            if (mem_available > mem_used+0x200000)
                size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
            if (size==0)
                size = 0x2000000*16;
        }
        else {
            size = __nx_heap_size;
        }

        Result rc = svcSetHeapSize(&addr, size);

        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
    }

    // Newlib
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = (char*)addr;
    fake_heap_end   = (char*)addr + size;
}

void __attribute__((weak)) __nx_win_init(void);
void __attribute__((weak)) userAppInit(void);

void __attribute__((weak)) __appInit(void)
{
    Result rc;

    // Initialize default services.
    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    if (!__libnx_hosversion_is_set()) {
        rc = setsysInitialize();
        if (R_SUCCEEDED(rc)) {
            SetSysFirmwareVersion fw;
            rc = setsysGetFirmwareVersion(&fw);
            if (R_SUCCEEDED(rc))
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
            setsysExit();
        }
    }

    rc = appletInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_AM));

    if (__nx_applet_type != AppletType_None) {
        rc = hidInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));
    }

    rc = timeInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_Time));

    __libnx_init_time();

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    fsdevMountSdmc();
    __libnx_init_cwd();

    if (&__nx_win_init) __nx_win_init();
    if (&userAppInit) userAppInit();
}

void __attribute__((weak)) userAppExit(void);
void __attribute__((weak)) __nx_gpu_exit(void);
void __attribute__((weak)) __nx_win_exit(void);

static bool g_driversClosed;

// A GPU driver linked into the program retires its device, then the default
// window closes. This runs after the program's own cleanup (atexit handlers,
// destructors, userAppExit), even if the program left graphics objects
// alive, and before the services that the driver uses close. It runs once.
static void _closeDrivers(void)
{
    if (g_driversClosed)
        return;
    g_driversClosed = true;
    if (&__nx_gpu_exit) __nx_gpu_exit();
    if (&__nx_win_exit) __nx_win_exit();
}

void __attribute__((weak)) __appExit(void)
{
    if (&userAppExit) userAppExit();
    _closeDrivers();

    // Cleanup default services.
    fsdevUnmountAll();
    fsExit();
    timeExit();
    hidExit();
    appletExit();
    smExit();
}

void __attribute__((weak)) __libnx_init(void* ctx, Handle main_thread, void* saved_lr)
{
    // Called by crt0.

    // Libnx initialization goes here.
    envSetup(ctx, main_thread, saved_lr);
    newlibSetup();
    virtmemSetup();
    __libnx_initheap();
    __libnx_init_thread();

    // Build argc/argv if present
    argvSetup();

#ifdef __SWITCH2__
    // With the mimalloc allocator (switch2/malloc/malloc.c) linked, the
    // rest of the heap goes to it before anything allocates. Without it,
    // newlib's malloc uses the heap through sbrk. libounce.a's copy of this
    // file (switch2/malloc_init) refers to the allocator strongly, so that
    // linking libounce.a brings it in; libounce_core.a's refers to it weakly.
#ifdef LIBOUNCE_MALLOC_LINKED
    extern void _mallocInitialize(void);
    _mallocInitialize();
#else
    extern void _mallocInitialize(void) __attribute__((weak));
    if (_mallocInitialize)
        _mallocInitialize();
#endif
#endif

    // Initialize services.
    __appInit();

    // Call constructors.
    void __libc_init_array(void);
    __libc_init_array();
}

#ifdef __SWITCH2__
void __attribute__((weak)) _gsvSyncExit(void);
void _serviceGuardCloseAll(void);
void _threadReleaseExited(void);
#endif

void __attribute__((weak)) NX_NORETURN __libnx_exit(int rc)
{
    // Clean up services.
    __appExit();

#ifdef __SWITCH2__
    // A program that replaces __appExit does not close the GPU driver and the
    // window; do it before their services close below.
    _closeDrivers();

    // Close every service that is still open: those the program opened and
    // did not close, and the default services when the program replaces
    // __appExit. Under hbloader the process outlives the program, so a
    // session left open would stay open for the next program.
    _serviceGuardCloseAll();

    // A GSV sync session left open belongs to the process, not the program:
    // the next program started from hbloader could not open one. This runs
    // after __appExit, so it also runs when a program replaces __appExit.
    if (&_gsvSyncExit) _gsvSyncExit();

    // Unmap the stacks of threads that ended but were never closed, so that
    // the heap pages under them are free for the next program. A thread
    // still running keeps its stack; hbloader reports it.
    _threadReleaseExited();
#endif

    __nx_exit(0, envGetExitFuncPtr());
}

