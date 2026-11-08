# Downstream release guide

This document describes releases of the `linuxcnc-ethercat/ethercat`
downstream tree and its Debian packages. It does not describe EtherLab
upstream releases.

## Branch and version model

- Develop and release on the existing `dev-X.Y` branch.
- Merge EtherLab `stable-X.Y` into the matching downstream branch. Keep
  downstream fixes and release tags on `dev-X.Y`.
- Forward relevant fixes from older release lines to newer ones.
- Keep `AC_INIT` at the upstream base version. Do not invent a new upstream
  version for downstream-only changes.
- Use the Debian revision for downstream releases: `1:1.6.13-3` contains
  upstream base `1.6.13`, Debian revision `3`, and epoch `1`.
- Tag the release/version-bump commit on `dev-X.Y`, not an arbitrary later
  commit.

Examples:

| Release | Debian changelog version | Git tag |
| --- | --- | --- |
| Downstream fix release | `1:1.6.13-3` | `v1.6.13-3` |
| Release candidate | `1:1.7.0~rc3-1` | `v1.7.0-rc3` |

The `3.0 (quilt)` entry in `debian/source/format` is the Debian source
package format. It is independent of the EtherCAT version and permits
Debian revisions such as `-3`.

## 1. Review the release delta

Fetch all remotes and inspect changes since the previous release:

```sh
git fetch --all --prune
git switch dev-1.6
git pull --ff-only
git log --oneline v1.6.13-2..HEAD
git diff --stat v1.6.13-2..HEAD
git log --oneline HEAD..upstream/stable-1.6
```

Check the equivalent branches and previous tag for a 1.7 release. Review
open upstream MRs and decide whether any must be included or may remain
independent downstream changes.

Compare packaging and workflows with the last known-good release. In
particular, check:

- master, userspace library, and DKMS exact-version dependencies;
- DKMS version paths, including epoch handling, and target build
  dependencies;
- main and EoE package identities and payloads;
- Debian and Ubuntu distribution coverage and version suffixes;
- amd64 and arm64 coverage;
- GitHub prerelease detection and apt repository dispatch;
- compatibility with LinuxCNC-EtherCAT dependencies.

## 2. Prepare the release commit

Add a new entry at the top of `debian/changelog`. Use `unstable` as the
distribution and `medium` urgency unless there is a reason to choose
otherwise. Summarize user-visible fixes, features, and packaging changes.

Do not change `AC_INIT` for a downstream-only revision. Commit only the
changelog update:

```text
release: 1:1.6.13-3
```

The timestamp must use Debian changelog format. `date -R` produces the
required form.

## 3. Validate locally

Build both source and binary packages from a clean checkout of the release
commit:

```sh
./script/build-deb.sh -s -j"$(nproc)"
```

Repeat with Clang and LLD when toolchain fixes are included:

```sh
CC=clang CXX=clang++ LDFLAGS=-fuse-ld=lld \
    ./script/build-deb.sh -s -j"$(nproc)"
```

Verify that the version is correct and inspect the outputs:

```sh
dpkg-parsechangelog -SVersion
dpkg-deb -f ../ethercat-master_1.6.13-3_amd64.deb Version Depends
dpkg-deb --contents ../ethercat-dkms_1.6.13-3_all.deb
```

The master package must require exactly matching DKMS and library versions.
The DKMS package must contain `dkms.conf`, the master sources, and its target
build dependencies. Check EoE variants separately when preparing both
variants outside CI.

Compilation alone is not sufficient release validation.

## 4. Run the complete remote matrix

Push `dev-X.Y` only with explicit authorization. Wait for the `Build .deb
packages` workflow on the exact release commit. All distribution,
architecture, and variant jobs must pass.

The workflow checks package dependencies and DKMS payloads. Do not tag while
any required job is pending or failed. The release-attachment job is expected
to remain skipped on a branch build.

## 5. Tag and publish

After the complete matrix passes, create an annotated tag on the release
commit:

```sh
git tag -a v1.6.13-3 <release-commit> -m "Release 1.6.13-3"
```

Push the tag only with explicit authorization:

```sh
git push origin v1.6.13-3
```

The tag workflow builds the full matrix again, creates or updates the GitHub
release, attaches the packages, generates release notes, and dispatches
`ingest-release` to `linuxcnc-ethercat/apt`. Do not create a duplicate manual
GitHub release.

Tags containing `pre`, `rc`, `alpha`, or `beta` produce prereleases. Stable
releases must not be marked prerelease.

## 6. Verify publication

Check all of the following before declaring the release complete:

- the tag workflow and every matrix job passed;
- the GitHub release exists with the expected package assets;
- stable releases are not prereleases, and RCs are prereleases;
- the apt `ingest-release` and Pages workflows passed;
- apt candidates show the new version for every supported distro and
  architecture;
- apt does not select prereleases for normal stable-package upgrades;
- installed master, library, and DKMS package versions match;
- representative target systems can install and build the DKMS module.

Record any skipped validation or hardware testing in the release discussion.
