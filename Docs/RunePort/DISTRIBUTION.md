# Open Rune — Android distribution materials

This is a modified SurrealEngine fork. This document describes the Android
prototype packaging; Windows, Linux, macOS, iOS and app-store distribution need
their own dependency and installation checks. These notices do not license
Rune's proprietary content or establish full game compatibility.

## Files to deliver together

`Android/build_apk.py` creates these files under the selected build's `apk/`:

- `open-rune.apk`, with complete notices in `assets/licenses/`.
- `notices.zip`, the same readable notices for recipients without an APK viewer.
- `lgpl-sources.zip`, the matching OpenAL Soft and SurrealVideo library sources,
  build files, replacement helper and this document.
- `distribution-manifest.json`, hashes tying the APK and companion archives together.

Give every APK recipient equivalent access to all four files at the same release
location. The archive must match the libraries in that build; a link to the
latest development sources alone does not replace the matching source archive.
Recipients retain the rights granted by the library licenses, including further
redistribution of those libraries and their sources.
Public Open Rune source: https://github.com/Mokomis/open-rune.
No contribution or notification to upstream is required by this packaging process.

The source archive exports only tracked files inside `SurrealVideo/` and
`Thirdparty/openal-soft/`, plus standalone build/replacement instructions. It
contains no proprietary game assets, user saves, signing keys or toolchains.
Sources are read from the packaging checkout, including tracked modifications;
rebuild the libraries from that checkout before packaging. New library source
files must be tracked before packaging. The archive manifest hashes every source
file and identifies the fork revision and changes in the library trees.

Networking builds (`OPENRUNE_ENABLE_NETWORK=ON`) also bundle the pinned Mbed TLS
3.6.7 and framework Apache-2.0 notices. The packager checks the build cache and
includes those notices in both the APK and notices archive.

## Library licenses and modifications

`libopenal.so` is OpenAL Soft, derived from the original OpenAL implementation;
copyrights include Copyright (C) 1999-2007 by authors, with individual authors
and notices retained in the source. It is distributed under GNU Library GPL
version 2 or later; `OpenAL.txt` contains version 2.

`libSurrealVideo.so` is SurrealVideo, a reduced FFmpeg derivative, under GNU
Lesser GPL version 2.1 or later. The full version 2.1 text is supplied as
`SurrealVideo-LGPL-2.1.txt`; copyright notices are preserved in its source files.
The Rune fork has not changed either library's source relative to its initial
SurrealEngine base as of October 24, 2026. Future library modifications must retain
the licenses and add prominent change/date notices to affected files.

The application uses these as separate shared libraries. It imposes no ban on
modifying the libraries or reverse engineering the application to debug such
modifications. The following method permits replacing an interface-compatible
library without disclosing the distributor's signing key.

## Build replacement ARM64 libraries

Extract `lgpl-sources.zip` into its own directory. Install CMake, a build tool,
and Android NDK 27.0.12077973. Set `RUNE_NDK` to that NDK's directory. From the
extracted directory:

```sh
cmake -S . -B build-libraries \
  -DCMAKE_TOOLCHAIN_FILE="$RUNE_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-libraries --target SurrealVideo OpenAL -j6
```

Outputs are `build-libraries/libSurrealVideo.so` and
`build-libraries/Thirdparty/openal-soft/libopenal.so`. Modify their sources and
repeat these commands, retaining the library ABI and original SONAME.

## Replace and sign with your own key

The helper copies the existing APK, replacing only the specified LGPL library
and dropping obsolete JAR signatures. It does not install anything or read game
data. It preserves all other entries; the resulting APK must be aligned and signed.

```sh
python3 replace_lgpl.py open-rune.apk replacement-unsigned.apk \
  --library build-libraries/libSurrealVideo.so
# Alternatively, or additionally: --library build-libraries/Thirdparty/openal-soft/libopenal.so
```

Set `RUNE_BUILD_TOOLS` to Android SDK build-tools 36.0.0, then:

```sh
"$RUNE_BUILD_TOOLS/zipalign" -P 16 -f 4 replacement-unsigned.apk replacement-aligned.apk
keytool -genkeypair -keystore my-test-key.jks -alias rune-test -keyalg RSA -validity 3650
"$RUNE_BUILD_TOOLS/apksigner" sign --ks my-test-key.jks --ks-key-alias rune-test \
  --out rune-replacement.apk replacement-aligned.apk
"$RUNE_BUILD_TOOLS/apksigner" verify --verbose rune-replacement.apk
```

Use your own key and enter passwords interactively. Android cannot update an
existing installation signed with a different key. Use a separate test device
or emulator; do not uninstall a data-bearing copy without exporting its data.
The app does not require the original distributor's signature to load these
libraries. Functional compatibility still depends on the changes you make.

## Notices and provenance

The notice bundle preserves the original engine, graphics, widget, SDL, audio,
shader, font and supporting-library notices. It also includes separately licensed
optional components present in OpenMPT's source tree; inclusion of a notice does
not assert that every optional component is enabled in this APK.

The Noto font copyright lines were verified against the actual TTF name tables.
The previous `Resources/noto/OFL.txt` incorrectly named Poppins. It now names
Noto Sans, and separate Armenian/Georgian OFL notices match those font files.
License text sources:

- https://github.com/notofonts/latin-greek-cyrillic/blob/main/OFL.txt
- https://github.com/notofonts/armenian/blob/main/OFL.txt
- https://github.com/notofonts/georgian/blob/main/OFL.txt
- https://github.com/mborgerding/kissfft/blob/master/LICENSES/BSD-3-Clause

KISS FFT's existing COPYING referenced BSD terms that were absent from the APK;
the full text is now included. PFFFT's UCAR/Julien Pommier notice is extracted from the
vendored source. Swappy's pinned official 2.1.3 AAR contains no separate NOTICE
file; its Apache-2.0 text and Android Open Source Project attribution are included.
GSR and CAS notices include the descriptions of our shader adaptations.

## Release checks

Before handing out a build, rebuild from the stated sources, verify the packaged
notices and archive hashes, and deliver the source/notices companions with it.
The four-file bundle is the release unit; old APKs/releases are not updated by
changing this repository. Publish companion sources with any older APK only after
matching them to that APK's exact library build.

The optional Vulkan validation layer is a development-only addition; do not
distribute it without separately including its dependency notices and checking
the resulting package. Store signing/repackaging restrictions, distribution
terms, proprietary-asset permissions and other platform-specific requirements
must be checked for the actual release channel. This packaging is not a blanket
legal clearance or a review of every reverse-engineering provenance question.

## Validation on October 24, 2026

- Packaged and signature-verified the notice update using the existing app key.
- Verified all 47 notice/manifest files against the APK and `notices.zip`.
- Verified release hashes and every source hash; the library source archive has
  372 entries and only the documented library trees and build/helper documents.
- Configured and compiled both SurrealVideo and OpenAL from the extracted archive
  for Android ARM64/API 28 using NDK 27.0.12077973.
- Replaced both libraries in a copy of the APK, aligned it and signed it with a
  fresh test key. Signature validation passed. All other payloads were preserved.
- Confirmed the release APK's native libraries and Java bytecode are byte-for-byte
  unchanged from the previous build. Resource changes are only the three Noto
  license files. The replacement helper rejects overwriting an existing output.
- No device install or runtime test was performed: the tablet was paused at the
  user's request. This validates source build and package replacement, not the
  behavior of arbitrary user-modified libraries on every device or app store.
