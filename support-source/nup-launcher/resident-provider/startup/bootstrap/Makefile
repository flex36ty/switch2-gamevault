# SPDX-License-Identifier: GPL-2.0-only
# The native bootstrap: a freestanding program that runs mapped into the
# acquisition process, linked with bootstrap.ld into a flat binary.
DEVKITPRO ?= /opt/devkitpro
LIBOUNCE ?= $(DEVKITPRO)/libounce
PREFIX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
OUT := build
CXXFLAGS := -Os -Wall -Wextra -Werror -Wno-missing-field-initializers -std=gnu++20 \
    -fPIE -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
    -ffunction-sections -fdata-sections -MMD -MP \
    -D__SWITCH__ -D__SWITCH2__ -I$(LIBOUNCE)/include
# The policy package and the SD writer are shared with HEN (../common).
OBJECTS := $(OUT)/main.o $(OUT)/policies.o $(OUT)/policy_package.o $(OUT)/sd_writer.o

all: $(OUT)/bootstrap.bin

$(OUT):
	mkdir -p $@

$(OUT)/%.o: source/%.cpp | $(OUT)
	$(PREFIX)g++ $(CXXFLAGS) -c $< -o $@

$(OUT)/%.o: ../common/%.cpp | $(OUT)
	$(PREFIX)g++ $(CXXFLAGS) -c $< -o $@

$(OUT)/bootstrap.elf: $(OBJECTS) bootstrap.ld $(LIBOUNCE)/lib/libounce_core.a
	$(PREFIX)g++ -nostdlib -Wl,--gc-sections,-T,bootstrap.ld,-Map,$(OUT)/bootstrap.map \
	    $(OBJECTS) $(LIBOUNCE)/lib/libounce_core.a -lc -o $@

$(OUT)/bootstrap.bin: $(OUT)/bootstrap.elf
	$(PREFIX)objcopy -O binary $< $@

clean:
	rm -rf $(OUT)

-include $(OBJECTS:.o=.d)
.PHONY: all clean
