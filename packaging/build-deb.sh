#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Builds a Debian package (.deb) of the CIX VA-API driver.
#
# Native build, on the board (Orange Pi 6 Plus, Ubuntu 26.04 arm64):
#     ./packaging/build-deb.sh --install-deps
#     sudo apt install ./dist/cix-va-driver_*_arm64.deb
#
# Cross build from an x86_64 host (arm64 multiarch dev packages or a sysroot):
#     ./packaging/build-deb.sh --cross [--sysroot /path/to/arm64-rootfs]
#
# Run "./packaging/build-deb.sh --help" for all the options.

set -euo pipefail
umask 022

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PKG_NAME="cix-va-driver"
HOMEPAGE="https://github.com/Trak-unixbased/cix_vaapi"
MAINTAINER="${DEBFULLNAME:-CIX VA-API packager} <${DEBEMAIL:-root@localhost}>"
DEB_REVISION="1"

BUILD_TYPE="Release"
CROSS=0
SYSROOT=""
CME="AUTO"
MVX_INCLUDE=""
OUT_DIR="$SRC_DIR/dist"
JOBS="$(nproc 2>/dev/null || echo 4)"
DO_STRIP=1
DO_LINT=0
INSTALL_DEPS=0
INSTALL_PKG=0
VERSION=""
ALLOW_ANY_ARCH=0
CLEAN=0

log()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWARNING:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Builds ${PKG_NAME}_<version>_arm64.deb in ${OUT_DIR}.

Options:
  --cross                 Cross-compile for arm64 with cc.cmake (aarch64-linux-gnu-g++).
  --sysroot DIR           arm64 sysroot used for a cross build.
  --cme AUTO|ON|OFF       Video processing through libcme (default: AUTO).
  --mvx-include DIR       Directory containing mvx-v4l2-controls.h (optional).
  --version VERSION       Upstream version (default: CMake project version + git).
  --revision N            Debian revision (default: ${DEB_REVISION}).
  --maintainer "N <m>"    Maintainer field (default: \$DEBFULLNAME <\$DEBEMAIL>).
  --build-type TYPE       CMake build type (default: ${BUILD_TYPE}).
  --output DIR            Output directory (default: ${OUT_DIR}).
  --jobs N                Parallel build jobs (default: ${JOBS}).
  --no-strip              Keep the debug symbols in the driver.
  --clean                 Remove the previous build directory first.
  --lint                  Run lintian on the result (if installed).
  --install-deps          Install the build dependencies with apt-get (sudo).
  --install               Install the package once built (native build, sudo).
  --allow-any-arch        Allow a native build on a non-arm64 host (testing only).
  -h, --help              Show this help.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --cross) CROSS=1 ;;
        --sysroot) SYSROOT="${2:?}"; shift ;;
        --cme) CME="${2:?}"; shift ;;
        --mvx-include) MVX_INCLUDE="${2:?}"; shift ;;
        --version) VERSION="${2:?}"; shift ;;
        --revision) DEB_REVISION="${2:?}"; shift ;;
        --maintainer) MAINTAINER="${2:?}"; shift ;;
        --build-type) BUILD_TYPE="${2:?}"; shift ;;
        --output) OUT_DIR="${2:?}"; shift ;;
        --jobs) JOBS="${2:?}"; shift ;;
        --no-strip) DO_STRIP=0 ;;
        --clean) CLEAN=1 ;;
        --lint) DO_LINT=1 ;;
        --install-deps) INSTALL_DEPS=1 ;;
        --install) INSTALL_PKG=1 ;;
        --allow-any-arch) ALLOW_ANY_ARCH=1 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1 (see --help)" ;;
    esac
    shift
done

case "$CME" in AUTO|ON|OFF) ;; *) die "--cme expects AUTO, ON or OFF" ;; esac

# ---------------------------------------------------------------------------
# Target architecture
# ---------------------------------------------------------------------------
if [ "$CROSS" -eq 1 ]; then
    ARCH="arm64"
    CXX_BIN="aarch64-linux-gnu-g++"
    STRIP_BIN="aarch64-linux-gnu-strip"
    NM_BIN="aarch64-linux-gnu-nm"
else
    ARCH="$(dpkg --print-architecture 2>/dev/null || echo unknown)"
    if [ "$ARCH" != "arm64" ] && [ "$ALLOW_ANY_ARCH" -ne 1 ]; then
        die "Native build on '$ARCH': this driver targets arm64 (use --cross from x86_64)"
    fi
    CXX_BIN="${CXX:-g++}"
    STRIP_BIN="strip"
    NM_BIN="nm"
fi

# ---------------------------------------------------------------------------
# Build dependencies
# ---------------------------------------------------------------------------
if [ "$INSTALL_DEPS" -eq 1 ]; then
    SUDO=""
    [ "$(id -u)" -eq 0 ] || SUDO="sudo"
    log "Installing build dependencies"
    if [ "$CROSS" -eq 1 ]; then
        $SUDO dpkg --add-architecture arm64
        $SUDO apt-get update
        $SUDO apt-get install -y cmake pkg-config dpkg-dev g++-aarch64-linux-gnu \
            libva-dev:arm64 libdrm-dev:arm64
    else
        $SUDO apt-get update
        $SUDO apt-get install -y build-essential cmake pkg-config dpkg-dev \
            libva-dev libdrm-dev
    fi
fi

for tool in cmake pkg-config dpkg-deb gzip "$CXX_BIN"; do
    command -v "$tool" >/dev/null 2>&1 || die "'$tool' is missing (try --install-deps)"
done

# ---------------------------------------------------------------------------
# Version: <cmake project version>[+git<date>.<hash>]-<revision>
# ---------------------------------------------------------------------------
if [ -z "$VERSION" ]; then
    VERSION="$(sed -n 's/^project([^)]*VERSION[[:space:]]\+\([0-9.]\+\).*/\1/p' "$SRC_DIR/CMakeLists.txt" | head -n1)"
    [ -n "$VERSION" ] || VERSION="0.0.0"
    if git -C "$SRC_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        GIT_DATE="$(git -C "$SRC_DIR" log -1 --format=%cd --date=format:%Y%m%d 2>/dev/null || true)"
        GIT_HASH="$(git -C "$SRC_DIR" rev-parse --short=8 HEAD 2>/dev/null || true)"
        if [ -n "$GIT_DATE" ] && [ -n "$GIT_HASH" ]; then
            VERSION="${VERSION}+git${GIT_DATE}.${GIT_HASH}"
            if [ -n "$(git -C "$SRC_DIR" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
                VERSION="${VERSION}.dirty"
            fi
        fi
    fi
fi
FULL_VERSION="${VERSION}-${DEB_REVISION}"
[[ "$FULL_VERSION" =~ ^[0-9][A-Za-z0-9.+~-]*$ ]] || die "Invalid Debian version: $FULL_VERSION"

if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
    SOURCE_DATE_EPOCH="$(git -C "$SRC_DIR" log -1 --format=%ct 2>/dev/null || date +%s)"
fi
export SOURCE_DATE_EPOCH

BUILD_DIR="$SRC_DIR/build-deb-$ARCH"
STAGE="$BUILD_DIR/pkgroot"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
DEB_FILE="$OUT_DIR/${PKG_NAME}_${FULL_VERSION}_${ARCH}.deb"

log "Package ${PKG_NAME} ${FULL_VERSION} (${ARCH})"

# ---------------------------------------------------------------------------
# Configure + build
# ---------------------------------------------------------------------------
if [ "$CLEAN" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(
    -S "$SRC_DIR" -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_INSTALL_PREFIX=/usr
    -DCIX_VAAPI_CME="$CME"
)
if [ "$CROSS" -eq 1 ]; then
    CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$SRC_DIR/cc.cmake")
    if [ -n "$SYSROOT" ]; then
        CMAKE_ARGS+=(-DCMAKE_SYSROOT="$SYSROOT")
    fi
fi
if [ -n "$MVX_INCLUDE" ]; then
    CMAKE_ARGS+=(-DCIX_VAAPI_MVX_INCLUDE_DIR="$MVX_INCLUDE")
fi

log "Configuring"
cmake "${CMAKE_ARGS[@]}"

log "Building with $JOBS jobs"
cmake --build "$BUILD_DIR" -j"$JOBS"

# ---------------------------------------------------------------------------
# Staging tree
# ---------------------------------------------------------------------------
log "Staging files"
rm -rf "$STAGE"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" --component runtime

DRIVERS_DIR="$(sed -n 's/^CIX_VAAPI_DRIVERS_DIR:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")"
[ -n "$DRIVERS_DIR" ] || die "CIX_VAAPI_DRIVERS_DIR not found in CMakeCache.txt"
DRIVER="$STAGE$DRIVERS_DIR/cix_drv_video.so"
[ -f "$DRIVER" ] || die "Driver not installed in $DRIVERS_DIR"

if command -v "$NM_BIN" >/dev/null 2>&1; then
    "$NM_BIN" -D --defined-only "$DRIVER" | grep -q '__vaDriverInit_' ||
        die "The driver does not export __vaDriverInit_*"
fi

chmod 0644 "$DRIVER"
if [ "$DO_STRIP" -eq 1 ]; then
    if command -v "$STRIP_BIN" >/dev/null 2>&1; then
        "$STRIP_BIN" --strip-unneeded --remove-section=.comment --remove-section=.note "$DRIVER"
    else
        warn "$STRIP_BIN not found: the driver is not stripped"
    fi
fi

DOC_DIR="$STAGE/usr/share/doc/$PKG_NAME"
install -D -m 0644 "$SCRIPT_DIR/70-cix-vaapi.rules" "$STAGE/usr/lib/udev/rules.d/70-cix-vaapi.rules"
install -D -m 0644 "$SCRIPT_DIR/90-cix-vaapi.conf" "$STAGE/usr/lib/environment.d/90-cix-vaapi.conf"
install -D -m 0644 "$SCRIPT_DIR/cix-vaapi.sh" "$STAGE/etc/profile.d/cix-vaapi.sh"
install -d -m 0755 "$DOC_DIR"
gzip -9n -c "$SRC_DIR/README.md" > "$DOC_DIR/README.md.gz"
# Local package: no ITP bug, and the udev rule is not tied to a modalias.
install -d -m 0755 "$STAGE/usr/share/lintian/overrides"
cat > "$STAGE/usr/share/lintian/overrides/$PKG_NAME" <<EOF
$PKG_NAME: initial-upload-closes-no-bugs
$PKG_NAME: appstream-metadata-missing-modalias-provide
EOF
chmod 0644 "$STAGE/usr/share/lintian/overrides/$PKG_NAME"

cat > "$DOC_DIR/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: cix_vaapi
Source: $HOMEPAGE

Files: *
Copyright: 2025-2026 Cix Technology Group Co., Ltd.
License: Apache-2.0
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 .
     http://www.apache.org/licenses/LICENSE-2.0
 .
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 .
 On Debian systems, the complete text of the Apache License 2.0 can be
 found in "/usr/share/common-licenses/Apache-2.0".
EOF
chmod 0644 "$DOC_DIR/copyright"

CHANGELOG_DATE="$(date -u -R -d "@$SOURCE_DATE_EPOCH" 2>/dev/null || date -u -R)"
cat <<EOF | gzip -9n > "$DOC_DIR/changelog.Debian.gz"
$PKG_NAME ($FULL_VERSION) unstable; urgency=medium

  * Local build of the CIX VA-API driver.

 -- $MAINTAINER  $CHANGELOG_DATE
EOF

# ---------------------------------------------------------------------------
# Package dependencies
# ---------------------------------------------------------------------------
SHLIBS_DEPENDS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    SHLIBS_TMP="$BUILD_DIR/shlibs"
    rm -rf "$SHLIBS_TMP"
    mkdir -p "$SHLIBS_TMP/debian"
    printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$PKG_NAME" "$PKG_NAME" \
        > "$SHLIBS_TMP/debian/control"
    if SHLIBS_OUT="$(cd "$SHLIBS_TMP" && dpkg-shlibdeps -O "$DRIVER" 2>/dev/null)"; then
        SHLIBS_DEPENDS="$(printf '%s\n' "$SHLIBS_OUT" | sed -n 's/^shlibs:Depends=//p')"
    fi
    rm -rf "$SHLIBS_TMP"
fi
if [ -z "$SHLIBS_DEPENDS" ]; then
    [ "$CROSS" -eq 1 ] || warn "dpkg-shlibdeps failed: using default library dependencies"
    SHLIBS_DEPENDS="libc6 (>= 2.34), libgcc-s1 (>= 3.0), libstdc++6 (>= 11)"
fi
DEPENDS="${SHLIBS_DEPENDS}, libva2 (>= 2.20) | cix-libva2"

INSTALLED_SIZE="$(du -sk --apparent-size "$STAGE" | cut -f1)"

install -d -m 0755 "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $FULL_VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $INSTALLED_SIZE
Depends: $DEPENDS
Recommends: vainfo
Suggests: ffmpeg, mpv, gstreamer1.0-plugins-bad
Provides: va-driver
Section: video
Priority: optional
Homepage: $HOMEPAGE
Description: VA-API driver for the CIX P1 (CD8180/CD8160) video engine
 Userspace VA-API driver (cix_drv_video.so) for the CIX Sky1 / P1 SoC found
 on the Orange Pi 6 Plus and the Radxa Orion O6. It drives the Arm MVX
 stateful V4L2 video processing unit:
  - decoding: H.264, VP9 (HEVC and AV1 with the CIX build of libva),
  - encoding: H.264, HEVC,
  - video post-processing through libcme when it is available.
 .
 The package selects the driver with LIBVA_DRIVER_NAME=cix and grants the
 "video" group access to the DMA-BUF heaps.
EOF

echo "/etc/profile.d/cix-vaapi.sh" > "$STAGE/DEBIAN/conffiles"

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

if [ "$1" = "configure" ]; then
    if command -v udevadm >/dev/null 2>&1 && [ -d /run/udev ]; then
        udevadm control --reload-rules || true
        udevadm trigger --subsystem-match=dma_heap --action=change || true
    fi
    echo "cix-va-driver: log out and back in (or run 'export LIBVA_DRIVER_NAME=cix')," \
         "and make sure your user is in the 'video' and 'render' groups." >&2
fi

exit 0
EOF

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e

case "$1" in
    remove|purge)
        if command -v udevadm >/dev/null 2>&1 && [ -d /run/udev ]; then
            udevadm control --reload-rules || true
        fi
        ;;
esac

exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"
chmod 0644 "$STAGE/DEBIAN/control" "$STAGE/DEBIAN/conffiles"

# Normalized permissions and timestamps (reproducible package)
find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

( cd "$STAGE" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' | LC_ALL=C sort -z |
    xargs -0 md5sum > DEBIAN/md5sums )
chmod 0644 "$STAGE/DEBIAN/md5sums"

# ---------------------------------------------------------------------------
# Build the .deb
# ---------------------------------------------------------------------------
log "Building $DEB_FILE"
rm -f "$DEB_FILE"
if dpkg-deb --help 2>/dev/null | grep -q -- '--root-owner-group'; then
    dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$DEB_FILE"
elif command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb -Zxz --build "$STAGE" "$DEB_FILE"
else
    die "dpkg-deb without --root-owner-group and no fakeroot available"
fi

dpkg-deb --info "$DEB_FILE" | sed -n '/Package:/,$p'
dpkg-deb --contents "$DEB_FILE"

if [ "$DO_LINT" -eq 1 ]; then
    if command -v lintian >/dev/null 2>&1; then
        log "Running lintian"
        lintian --tag-display-limit 0 "$DEB_FILE" || true
    else
        warn "lintian is not installed"
    fi
fi

if [ "$INSTALL_PKG" -eq 1 ]; then
    [ "$CROSS" -eq 0 ] || die "--install is only possible for a native build"
    SUDO=""
    [ "$(id -u)" -eq 0 ] || SUDO="sudo"
    log "Installing $DEB_FILE"
    $SUDO apt-get install -y "$DEB_FILE"
fi

log "Done: $DEB_FILE"
cat <<EOF

Install on the board:
    sudo apt install ./$(basename "$DEB_FILE")
Check:
    LIBVA_DRIVER_NAME=cix vainfo --display drm --device /dev/dri/renderD128
EOF
