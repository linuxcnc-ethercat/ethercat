#!/bin/bash
# Build Debian binary packages (ethercat, libethercat1, libethercat-dev,
# ethercat-dkms) in the parent directory.
#
# Requires: debhelper (>=13), autoconf, automake, libtool, pkg-config,
# dkms (build-time dep).
#
# Usage: ./script/build-deb.sh [-j JOBS]

set -euo pipefail

JOBS="${JOBS:-$(nproc)}"

while getopts "j:" opt; do
    case "$opt" in
        j) JOBS="$OPTARG" ;;
        *) echo "usage: $0 [-j JOBS]" >&2; exit 1 ;;
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
dpkg-buildpackage -us -uc -b -d

echo
echo "Packages written next to the source tree:"
ls -1 ../ethercat_*_*.deb ../ethercat-dkms_*_all.deb \
      ../libethercat1_*_*.deb ../libethercat-dev_*_*.deb 2>/dev/null
