# SPDX-License-Identifier: GPL-2.0-only
# kaiHEN's binaries, built in the Switch 2 devkit's image.
#   docker build --output out .
#     ->  out/ro/main, out/hen/main, out/bootstrap.bin, out/libkaihen/
#   docker build --build-arg RELEASE=1 --output out .
#     ->  out/kaiHEN.zip, with nx-hbloader, nx-hbmenu and OunceShell at the
#         commits below (release/Makefile)
#   docker build --target build -t kaihen-build .   (the build environment)
# DEVKIT_IMAGE is the devkit's image: build it from the devkit repository, tagged
# ounce-devkit, or name another with --build-arg DEVKIT_IMAGE=...
ARG DEVKIT_IMAGE=ounce-devkit:latest
FROM ${DEVKIT_IMAGE} AS build
ARG RELEASE=0
ARG HBLOADER_URL=https://github.com/ouncebrew/ounce-hbloader.git
ARG HBLOADER_COMMIT=95f0e5dfbbcd28d678d30c99eeb5344feb0f6e71
ARG HBMENU_URL=https://github.com/ouncebrew/ounce-hbmenu.git
ARG HBMENU_COMMIT=d7e10c028b53da17867615ee0a3c453b9cf9464a
ARG OUNCESHELL_URL=https://github.com/kurumuz/ounceshell.git
ARG OUNCESHELL_COMMIT=b2655caf6f8e48084bde84f18c28f834358f8286
COPY . /src
WORKDIR /src
RUN make -j"$(nproc)" \
    && mkdir -p /out \
    && if [ "$RELEASE" = 1 ]; then \
        make -j"$(nproc)" -f release/Makefile RELEASE_ZIP=/out/kaiHEN.zip; \
    else \
        mkdir -p /out/ro /out/hen /out/libkaihen \
        && cp ro/build/main /out/ro/main \
        && cp hen/build/main /out/hen/main \
        && cp bootstrap/build/bootstrap.bin /out/bootstrap.bin \
        && cp libkaihen/build/kaihen-jit.o libkaihen/build/libkaihen.a \
              libkaihen/build/libkaihen_plugin.a /out/libkaihen/; \
    fi

FROM scratch
COPY --from=build /out/ /
