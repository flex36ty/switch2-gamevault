DEVKITPRO ?= /c/devkitPro
LIBOUNCE ?= ../libounce-source/ouncebrew-libounce-d1718bb/nx
LIBOUNCE := $(abspath $(LIBOUNCE))
include $(LIBOUNCE)/switch2_cpu.mk
PREFIX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
OUT := build-kaihen
ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CPPFLAGS := -D__SWITCH__ -D__SWITCH2__ -DWANT_APPLET -I../include -I../../celeste-resident-hen-source/libkaihen/include -I$(LIBOUNCE)/include -I$(LIBOUNCE)/include/switch -I$(LIBOUNCE)/source/services
CFLAGS := $(ARCH) -O2 -g -Wall -Wextra -Werror -Wno-misleading-indentation -ffunction-sections -fdata-sections -MMD -MP
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -Wno-missing-field-initializers
OBJECTS := $(patsubst source/%.cpp,$(OUT)/%.o,$(wildcard source/*.cpp)) $(patsubst source/%.c,$(OUT)/%.o,$(wildcard source/*.c))
all: backup-workbench.nro
$(OUT):
	mkdir -p $@
$(OUT)/%.o: source/%.cpp | $(OUT)
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(OUT)/%.o: source/%.c | $(OUT)
	$(PREFIX)gcc $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/switch.specs: $(LIBOUNCE)/switch.specs | $(OUT)
	sed 's|%:getenv(DEVKITPRO /libnx/switch.ld)|$(shell cygpath -m $(LIBOUNCE)/switch.ld)|' $< > $@
$(OUT)/mtp.elf: $(OBJECTS) $(OUT)/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=$(OUT)/switch.specs -Wl,--gc-sections,-Map,$(OUT)/mtp.map $(OBJECTS) -L$(LIBOUNCE)/lib -lounce_core -o $@
$(OUT)/mtp.nacp: Makefile | $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create 'Backup Workbench' 'Local diagnostic' '0.1.1' $@
backup-workbench.nro: $(OUT)/mtp.elf $(OUT)/mtp.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/mtp.nacp
-include $(OBJECTS:.o=.d)
.PHONY: all




