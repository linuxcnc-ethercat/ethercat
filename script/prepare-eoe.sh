#!/bin/sh
# Mutate debian/ in-place to produce an EoE-enabled variant of the
# packages. Renames binary packages with the -eoe suffix and flips
# --disable-eoe to --enable-eoe in debian/rules and debian/dkms.conf.
#
# Run once before dpkg-buildpackage. Idempotent within a clean tree;
# do not run twice without resetting.
#
# Resulting packages:
#   ethercat-master      -> ethercat-master-eoe
#   libethercat          -> libethercat-eoe
#   libethercat-dev      -> libethercat-eoe-dev
#   ethercat-dkms        -> ethercat-dkms-eoe
# All four Conflict/Provide/Replace their non-eoe counterparts so the
# two variants cannot be installed side by side.

set -eu

cd "$(dirname "$0")/.."

# debian/control: rename packages and inter-package deps.
sed -i \
    -e 's/^Package: ethercat-master$/Package: ethercat-master-eoe/' \
    -e 's/^Package: libethercat$/Package: libethercat-eoe/' \
    -e 's/^Package: libethercat-dev$/Package: libethercat-eoe-dev/' \
    -e 's/^Package: ethercat-dkms$/Package: ethercat-dkms-eoe/' \
    -e 's/^ ethercat-dkms (= /\&__EOE_DKMS_DEP__/' \
    -e 's/^ libethercat (= /\&__EOE_LIB_DEP__/' \
    debian/control

# Now restore the inter-package version deps but pointed at -eoe names.
sed -i \
    -e 's/&__EOE_DKMS_DEP__/ ethercat-dkms-eoe (= /' \
    -e 's/&__EOE_LIB_DEP__/ libethercat-eoe (= /' \
    debian/control

# Rewrite Provides/Replaces/Conflicts for ethercat-master-eoe so it
# Conflicts with the non-eoe ethercat-master and the original ethercat.
sed -i \
    -e '/^Package: ethercat-master-eoe$/,/^Description:/ {
        s/^Provides: ethercat$/Provides: ethercat, ethercat-master/
        s/^Replaces: ethercat$/Replaces: ethercat, ethercat-master/
        s/^Conflicts: ethercat$/Conflicts: ethercat, ethercat-master/
    }' debian/control

# libethercat-eoe Conflicts with libethercat (same SONAME, same path).
# Append after the libethercat-eoe Description block by patching its
# stanza header.
python3 - <<'PY'
import re, pathlib
p = pathlib.Path('debian/control')
text = p.read_text()

def add_conflicts_after_header(text, package, conflicts_pkgs):
    """Insert Conflicts/Provides/Replaces lines into the named package
    stanza, just before the Description: line. No-op if already there."""
    pat = re.compile(
        rf'(^Package: {re.escape(package)}\n(?:(?!Description:)[^\n]*\n)+)(Description:)',
        re.MULTILINE)
    def repl(m):
        header = m.group(1)
        if 'Conflicts:' in header:
            return m.group(0)
        addition = (
            f'Conflicts: {", ".join(conflicts_pkgs)}\n'
            f'Provides: {", ".join(conflicts_pkgs)}\n'
            f'Replaces: {", ".join(conflicts_pkgs)}\n'
        )
        return header + addition + m.group(2)
    return pat.sub(repl, text, count=1)

text = add_conflicts_after_header(text, 'libethercat-eoe',     ['libethercat'])
text = add_conflicts_after_header(text, 'libethercat-eoe-dev', ['libethercat-dev'])
text = add_conflicts_after_header(text, 'ethercat-dkms-eoe',   ['ethercat-dkms'])
p.write_text(text)
PY

# debian/rules: flip --disable-eoe to --enable-eoe in the userspace
# configure invocation, and switch -p ethercat-master to the new name.
sed -i \
    -e 's/--disable-eoe/--enable-eoe/' \
    -e 's/-p ethercat-master /-p ethercat-master-eoe /' \
    debian/rules

# debian/dkms.conf: flip --disable-eoe and rename PACKAGE_NAME.
sed -i \
    -e 's/--disable-eoe/--enable-eoe/' \
    -e 's/^PACKAGE_NAME="ethercat"$/PACKAGE_NAME="ethercat-eoe"/' \
    debian/dkms.conf

# Rename per-package debhelper helper files.
for ext in install postinst postrm; do
    if [ -f "debian/ethercat-master.$ext" ]; then
        mv "debian/ethercat-master.$ext" "debian/ethercat-master-eoe.$ext"
    fi
done
if [ -f debian/libethercat.install ]; then
    mv debian/libethercat.install debian/libethercat-eoe.install
fi
if [ -f debian/libethercat-dev.install ]; then
    mv debian/libethercat-dev.install debian/libethercat-eoe-dev.install
fi
for ext in postinst prerm; do
    if [ -f "debian/ethercat-dkms.$ext" ]; then
        mv "debian/ethercat-dkms.$ext" "debian/ethercat-dkms-eoe.$ext"
    fi
done

echo "Prepared EoE variant."
