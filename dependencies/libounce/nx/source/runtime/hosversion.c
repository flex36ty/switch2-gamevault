#include "runtime/hosversion.h"

static u32 g_hosVersion;

// Marker bits above MAKEHOSVERSION's 24 bits: BIT(31) Atmosphère, BIT(30)
// kaiHEN's game-role loader (env.c _envSetHosVersion).
#define HOSVERSION_MARKERS (BIT(31) | BIT(30))

#if defined(__SWITCH2__)
// Every Switch 2 runs system version 20.0.0 or later (kaiHEN's hbloader
// passes 20.0.0 to NROs), so version-gated commands take their 20.0.0+
// form there. Until startup learns the exact version, report that minimum
// rather than zero.
#define HOSVERSION_DEFAULT MAKEHOSVERSION(20,0,0)
#else
#define HOSVERSION_DEFAULT 0
#endif

static u32 _hosversionLoad(void)
{
    return __atomic_load_n(&g_hosVersion, __ATOMIC_SEQ_CST);
}

u32 hosversionGet(void)
{
    const u32 version = _hosversionLoad() & ~HOSVERSION_MARKERS;
    return version ? version : HOSVERSION_DEFAULT;
}

// Whether startup set the version. If it did not, init.c asks set:sys.
bool __libnx_hosversion_is_set(void)
{
    return (_hosversionLoad() & ~HOSVERSION_MARKERS) != 0;
}

void hosversionSet(u32 version)
{
    __atomic_store_n(&g_hosVersion, version, __ATOMIC_SEQ_CST);
}

bool hosversionIsAtmosphere(void)
{
    return _hosversionLoad() & BIT(31);
}

bool hosversionIsKaiHEN(void)
{
    return _hosversionLoad() & BIT(30);
}
