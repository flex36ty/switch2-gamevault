#---------------------------------------------------------------------------------
# The compiler flags for the Switch 2's CPU, which has Cortex-A78C cores.
#
# switch2_rules includes this file. A Makefile that does not use the rules can
# include it from $(DEVKITPRO)/libounce/switch2_cpu.mk. Use SWITCH2_CPU in ARCH
# in place of devkitPro's Switch 1 flags (-march=armv8-a+crc+crypto
# -mtune=cortex-a57):
#
#   ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
#
# The A78C's AES and SHA instructions are an optional extension; +crypto
# enables them, as devkitPro's flags do. The Switch 1 flags still build
# working code, for an older CPU.
#
# This line is the only place that sets the flags: libounce's build,
# switch2_rules, switch2vars.sh and the CMake toolchain all read it.
#---------------------------------------------------------------------------------
SWITCH2_CPU	:=	-mcpu=cortex-a78c+crypto
