.DEFAULT_GOAL := all
DEVKITPRO ?= /c/devkitPro
LIBOUNCE ?= ../dependencies/libounce/nx
LIBOUNCE := $(abspath $(LIBOUNCE))
include $(LIBOUNCE)/switch2_cpu.mk
PREFIX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
OUT := build-kaihen
ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CPPFLAGS := -Ivendor/libsmb2/include  -D__SWITCH__ -D__SWITCH2__ -DWANT_APPLET -Iinclude -Ivendor/mtp/include -idirafter $(DEVKITPRO)/libnx/include -I$(DEVKITPRO)/portlibs/switch/include -I$(LIBOUNCE)/include -I$(LIBOUNCE)/include/switch -I$(LIBOUNCE)/source/services
CFLAGS := $(ARCH) -O2 -g -Wall -Wextra -Werror -Wno-misleading-indentation -ffunction-sections -fdata-sections -MMD -MP
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -Wno-missing-field-initializers
OBJECTS := $(patsubst source/%.cpp,$(OUT)/%.o,$(wildcard source/*.cpp)) $(patsubst source/%.c,$(OUT)/%.o,$(wildcard source/*.c))
MTP_CPP := $(wildcard vendor/mtp/source/*.cpp)
MTP_C := $(wildcard vendor/mtp/source/*.c)
OBJECTS += $(patsubst vendor/mtp/source/%.cpp,$(OUT)/mtp-%.o,$(MTP_CPP)) $(patsubst vendor/mtp/source/%.c,$(OUT)/mtp-%.o,$(MTP_C))
$(OUT)/mtp-%.o: vendor/mtp/source/%.cpp | $(OUT)
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -Wno-error -c $< -o $@
$(OUT)/mtp-%.o: vendor/mtp/source/%.c | $(OUT)
	$(PREFIX)gcc $(CPPFLAGS) $(CFLAGS) -Wno-error -c $< -o $@
all: game-vault-companion-v043.nro
$(OUT):
	mkdir -p $@
$(OUT)/%.o: source/%.cpp | $(OUT)
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(OUT)/%.o: source/%.c | $(OUT)
	$(PREFIX)gcc $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/switch.specs: $(LIBOUNCE)/switch.specs | $(OUT)
	sed 's|%:getenv(DEVKITPRO /libnx/switch.ld)|$(shell cygpath -m $(LIBOUNCE)/switch.ld)|' $< > $@
$(OUT)/mtp.elf: $(OBJECTS) $(OUT)/switch.specs vendor/libsmb2/build-vault/lib/libsmb2.a
	$(PREFIX)g++ $(ARCH) -specs=$(OUT)/switch.specs -Wl,--gc-sections,-Map,$(OUT)/mtp.map $(OBJECTS) vendor/libsmb2/build-vault/lib/libsmb2.a -L$(DEVKITPRO)/portlibs/switch/lib -lcurl -L$(LIBOUNCE)/lib -lounce_core -L$(DEVKITPRO)/portlibs/switch/lib -lz -o $@
$(OUT)/mtp.nacp: Makefile | $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create 'Game Vault' 'Game Vault Project' '0.4.3' $@
game-vault-companion-v043.nro: $(OUT)/mtp.elf $(OUT)/mtp.nacp assets/game-vault-icon.jpg
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/mtp.nacp --icon=assets/game-vault-icon.jpg
-include $(OBJECTS:.o=.d)
.PHONY: all



$(OUT)/ui.o: CXXFLAGS += -Wno-unused-function -Wno-maybe-uninitialized


$(OUT)/mtp_mode.o: CXXFLAGS += -Wno-error
