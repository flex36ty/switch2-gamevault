# SPDX-License-Identifier: GPL-2.0-only
# Builds kaiHEN with the Switch 2 devkit:
#   ro/build/main                 RO
#   hen/build/main                HEN
#   bootstrap/build/bootstrap.bin the native bootstrap
#   libkaihen/build/            the homebrew API library the devkit ships
#
#   make                     with an installed devkit ($DEVKITPRO/libounce)
#   make LIBOUNCE=DIR     with a libounce build (a checkout's nx folder,
#                            after `make SWITCH2=1 lib/libounce_core.a` there)
# The Dockerfile runs this in the devkit's image.
DEVKITPRO ?= /opt/devkitpro
LIBOUNCE ?= $(DEVKITPRO)/libounce
export DEVKITPRO LIBOUNCE

COMPONENTS := libkaihen ro hen bootstrap

all: $(COMPONENTS)

$(COMPONENTS):
	$(MAKE) -C $@

clean:
	for component in $(COMPONENTS); do $(MAKE) -C $$component clean; done

.PHONY: all clean $(COMPONENTS)
