# libounce: libnx with Switch 2 support.
#   make, make install         the Switch 2 library, into $(DEVKITPRO)/libounce
#   make switch1, make install-switch1
#                              libnx for Switch 1, into $(DEVKITPRO)/libnx
#   make dist-src              a source archive of this commit
all:
	$(MAKE) -C nx/ switch2

install:
	$(MAKE) -C nx/ install-switch2

switch1:
	$(MAKE) -C nx/ switch1

install-switch1:
	$(MAKE) -C nx/ install-switch1

dist-src:
	$(MAKE) -C nx/ dist-src

clean:
	$(MAKE) -C nx/ clean

.PHONY: all install switch1 install-switch1 dist-src clean
