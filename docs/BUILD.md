# Building Game Vault

Use devkitPro MSYS2 on Windows, devkitA64, Switch portlibs (libcurl/zlib), cmake,
make, and the bundled libounce source. The shipped Makefile uses Windows cygpath.
Toolchain binaries and portlibs are not bundled.

From the extracted source root in the devkitPro MSYS2 shell:

```sh
export DEVKITPRO=/c/devkitPro
make -C dependencies/libounce -j2
cmake -S console/vendor/libsmb2 -B console/vendor/libsmb2/build-vault -G 'Unix Makefiles' \
  -DCMAKE_TOOLCHAIN_FILE=/c/devkitPro/cmake/Switch.cmake \
  -DCMAKE_DEPENDS_USE_COMPILER=FALSE \
  -DCMAKE_C_FLAGS='-O2 -fPIE -mtp=soft -mcpu=cortex-a78c+crypto -D__SWITCH__' \
  -DBUILD_SHARED_LIBS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_UTILS=OFF \
  -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF
cmake --build console/vendor/libsmb2/build-vault -j2
make -C console -j2
```

Output: `console/game-vault-companion-v045.nro`.
libsmb2 includes the Switch randomGet adaptation and source/licenses; libounce
includes the USB initialization adaptations used by this app. Keep these copies
when rebuilding. Supporting HEN/plugin source snapshots are under support-source;
they are reference material, not a claim that every bundled binary can be rebuilt
bit-for-bit from one command. The supplied known-working HEN binary is unchanged.
