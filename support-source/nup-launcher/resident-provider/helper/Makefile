.DEFAULT_GOAL := all
DEVKITPRO ?= /c/devkitPro
LIBOUNCE ?= ../../../investigation/libounce-source/ouncebrew-libounce-d1718bb/nx
LIBOUNCE := $(abspath $(LIBOUNCE))
include $(LIBOUNCE)/switch2_cpu.mk
PREFIX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
OUT := build-kaihen
ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CPPFLAGS := -D__SWITCH__ -D__SWITCH2__  -I$(DEVKITPRO)/portlibs/switch/include -I../include -I../../provider-test/include -I../startup/libkaihen/include -I../startup/libkaihen/source -I$(LIBOUNCE)/include -I$(LIBOUNCE)/external/bsd/include -I$(LIBOUNCE)/include/switch -I$(LIBOUNCE)/source/services
CFLAGS := $(ARCH) -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -MMD -MP
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -Wno-missing-field-initializers
OBJECTS := $(patsubst source/%.cpp,$(OUT)/%.o,$(wildcard source/*.cpp)) $(patsubst source/%.c,$(OUT)/%.o,$(wildcard source/*.c))
OBJECTS += $(OUT)/plugin_runtime.o
$(OUT)/plugin_runtime.o: ../startup/libkaihen/source/plugin_runtime.cpp | $(OUT)
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
all: storage-provider.nso
$(OUT):
	mkdir -p $@
$(OUT)/%.o: source/%.cpp | $(OUT)
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(OUT)/%.o: source/%.c | $(OUT)
	$(PREFIX)gcc $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/switch.specs: $(LIBOUNCE)/switch.specs | $(OUT)
	sed 's|%:getenv(DEVKITPRO /libnx/switch.ld)|$(shell cygpath -m $(LIBOUNCE)/switch.ld)|' $< > $@
$(OUT)/mtp.elf: $(OBJECTS) $(OUT)/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=$(OUT)/switch.specs -Wl,--gc-sections,-Map,$(OUT)/mtp.map $(OBJECTS) -L$(LIBOUNCE)/lib -lounce_core -L$(DEVKITPRO)/portlibs/switch/lib -lz -o $@
$(OUT)/mtp.nacp: | $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create 'Resident Provider Client' 'Local launcher project' '1.0.0' $@
storage-provider.nso: $(OUT)/mtp.elf
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
-include $(OBJECTS:.o=.d)
.PHONY: all
