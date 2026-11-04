#!/bin/bash
# Build Debian binary packages (ethercat, libethercat1, libethercat-dev,
# ethercat-dkms) in the parent directory. With -s, also build the source
# package; the .orig tarball is generated from HEAD minus debian/.
#
# Requires: debhelper (>=13), autoconf, automake, libtool, pkg-config,
# dkms (build-time dep).
#
# Usage: ./script/build-deb.sh [-s] [-j JOBS]

set -euo pipefail

JOBS="${JOBS:-$(nproc)}"
SOURCE=0

while getopts "sj:" opt; do
    case "$opt" in
        s) SOURCE=1 ;;
        j) JOBS="$OPTARG" ;;
        *) echo "usage: $0 [-s] [-j JOBS]" >&2; exit 1 ;;
    esac
done

cd "$(dirname "$0")/.."

if ! command -v dpkg-buildpackage >/dev/null; then
    echo "dpkg-dev is not installed." >&2
    exit 1
fi

missing=()
for pkg in debhelper autoconf automake libtool pkg-config dkms; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        missing+=("$pkg")
    fi
done
if [ "${#missing[@]}" -gt 0 ]; then
    echo "Missing build dependencies: ${missing[*]}" >&2
    echo "Install with: sudo apt install ${missing[*]}" >&2
    exit 1
fi

export DEB_BUILD_OPTIONS="parallel=${JOBS}"
if [ "$SOURCE" = 1 ]; then
    # 3.0 (quilt) needs ../ethercat_<upstream>.orig.tar.xz. Strip the
    # epoch and Debian revision from the changelog version.
    upstream=$(dpkg-parsechangelog -SVersion | sed -e 's/^[0-9]*://' -e 's/-[^-]*$//')
    git archive --format=tar --prefix="ethercat-${upstream}/" HEAD \
        ':(exclude)debian' | xz -T0 > "../ethercat_${upstream}.orig.tar.xz"
    dpkg-buildpackage -us -uc -d
else
    dpkg-buildpackage -us -uc -b -d
fi

echo
echo "Packages written next to the source tree:"
ls -1 ../ethercat-master*_*.deb ../ethercat-dkms*_all.deb \
      ../libethercat*_*.deb 2>/dev/null
