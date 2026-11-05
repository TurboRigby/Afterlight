# Licensing and distribution

Afterlight's original code, shaders, build/tool scripts, documentation, sample
scenes and procedural assets are licensed under **MPL-2.0**. [LICENSE](../LICENSE)
is Mozilla's complete, unmodified text, downloaded from its [official plain-text
copy](https://www.mozilla.org/media/MPL/2.0/index.f75d2927d3c1.txt).
[NOTICE](../NOTICE) covers files that cannot contain comments. Individual source
files carry MPL notices. Preserve existing notices when editing or copying them.
New original source files should carry `SPDX-License-Identifier: MPL-2.0` or the
Exhibit A notice. Do not add Exhibit B's incompatibility notice.

Dependencies and the font retain their original licenses; see
[third-party notices](../THIRD_PARTY_NOTICES.md). Do not replace their headers
with Afterlight's MPL notice.

## Binary builds and exports

MPL section 3.2 requires recipients of covered binaries to be told how to obtain
the corresponding covered source, including modifications. CMake creates
`source/afterlight-source.zip` from this checkout and puts it, LICENSE, NOTICE,
and the dependency notices in each runnable app. On macOS they are inside
`Contents/Resources`; elsewhere they sit beside the executable. Distribute these
files together. The archive excludes build outputs, Git history and user CMake
presets. It includes build scripts, shaders, vendored sources and original sample
assets so recipients can modify the covered implementation.

Rebuild after source changes before distributing. Do not edit source during a
build or replace a binary without updating its corresponding source archive.
If distributing a static library or a game module separately, include the
archive and notices beside it, or provide another durable way for recipients to
obtain that exact covered source. Keep the original copyright, license, patent
and warranty notices. Additional executable terms must not restrict the MPL
rights to covered source. The license itself controls these obligations; see
[Mozilla's FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/).

The export tool preserves the player's build-time source archive; it does not
substitute whatever engine checkout happens to be present at export time. It
refuses a player missing that archive. Its README tells recipients where to
find the source and licenses.

## C game projects

MPL copyleft operates at the file level. Independent game files can use a
different license, including a proprietary license. Linking against Afterlight
does not by itself apply MPL to every game file. Files copied from, or containing
code from, MPL templates remain covered, including modifications to those files.

The wizard preserves MPL headers on copied public headers and `game/game.c`,
copies LICENSE and a scoped NOTICE, and creates a module source archive during
the game build. This archive contains marked MPL files from `game/`, `include/`,
`tools/`, and the generated build script and notices. It does not automatically
relicense or archive independent unmarked files. Keep the notices on covered
files and mark new files containing covered code. The exporter copies the
module's adjacent `<module filename>.source.zip` to `source/game-source.zip`.

For a custom build system or additional covered modules, supply corresponding
covered source alongside each module as `<module filename>.source.zip`, or
arrange your own source availability notice and distribution. Verify that the
archive includes all covered modifications and any inputs needed to modify them.
Afterlight cannot infer the license or provenance of an arbitrary native module
or imported asset. The engine archive includes the repository's demo and
playground modules. It is not a substitute for source to your modified templates.

## Checking a release

Run the licensing and project export tests, inspect the resulting archive and
notices, and retain them with the shipped binaries. On macOS the exporter copies
installed runtime LICENSE/COPYING and NOTICE files and records modified library
paths in the third-party notice. Check dependency-specific requirements when
adding libraries or shipping on a different platform. No build action publishes
source to an external service.
