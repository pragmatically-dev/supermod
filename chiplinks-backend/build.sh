#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Mount MIGRATION/ (parent) so vendor/sqlite/ is reachable via $$PWD/../vendor
# from the .pro. Build artifacts land in chiplinks-backend/ (workdir).
MIGRATION_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$SCRIPT_DIR"

IMAGE="eeems/remarkable-toolchain:latest-rm2"

echo "[build.sh] Building chiplinks-backend.so via $IMAGE"

# Prevent MSYS/git-bash from mangling Unix paths into Windows paths when
# they're passed as docker args. Without this, `-w /src` becomes
# `C:/Program Files/Git/src` and docker rejects it.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

docker run --rm \
    -v "${MIGRATION_DIR}:/src" \
    -w /src/chiplinks-backend \
    "$IMAGE" \
    bash -lc '
        set -e
        if [ ! -d /tmp/xovi ]; then
            apt-get update -qq && apt-get install -y -qq git python3
            git clone https://github.com/asivery/xovi /tmp/xovi
        fi
        export XOVI_REPO=/tmp/xovi
        . /opt/codex/*/*/environment-setup-*
        qmake6
        # serial build: xovigen genera xovi.c + xovi.h pero PRE_TARGETDEPS solo
        # rastrea xovi.c, y entry.c #incluye xovi.h. Con -j paralelo, make
        # arranca a compilar entry.c antes de que xovi.h aterrice.
        # make clean primero para forzar relink cuando solo cambian LIBS del .pro
        # (los .o no cambian, make decide no relinkear).
        make clean >/dev/null 2>&1 || true
        # Remove any prior .so FIRST: a failed make would otherwise leave a stale
        # .so on disk, and the [ -f ...so ] check below would report that old build
        # as "OK" (this masked a real xovigen failure once — a420e0f's broken .xovi).
        rm -f chiplinks-backend.so
        make -j1
        echo "=== build artifacts ==="
        ls -la build/xovi/ chiplinks-backend.so 2>&1 || true
    '

if [ -f chiplinks-backend.so ]; then
    echo "[build.sh] OK: chiplinks-backend.so produced ($(stat -c %s chiplinks-backend.so) bytes)"
else
    echo "[build.sh] FAIL: chiplinks-backend.so not produced"
    exit 1
fi
