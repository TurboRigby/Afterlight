# Third-party notices

Afterlight uses the following dependencies. These components retain their own
licenses; Afterlight's MPL-2.0 does not replace them. Preserve the original
copyright, license and warranty notices when redistributing these components.

| Component | Location | License |
| --- | --- | --- |
| cgltf | `third_party/cgltf/` | MIT; see its `LICENSE` |
| cJSON 1.7.19 | `third_party/cjson/` | MIT; see its `LICENSE` |
| Nuklear 4.13.4 | `third_party/nuklear/` | MIT option; see its dual `LICENSE` |
| stb_image 2.30 | `third_party/stb/` | MIT option; see its dual `LICENSE` |
| Fira Sans Regular | `editor/assets/` | SIL OFL 1.1; see `OFL.txt` |
| SDL3 | External build/runtime dependency | zlib; preserve the supplied `LICENSE.txt` |
| SMAA shader and lookup tables | `third_party/smaa` | MIT; original headers and LICENSE retained |
| Vulkan Loader | External runtime dependency | Apache-2.0 and upstream exceptions; preserve its supplied license/notices |
| MoltenVK (macOS) | External runtime dependency | Apache-2.0; preserve its supplied license/notices |
| AMD FidelityFX Super Resolution 1.0.2 | Optional external shader SDK for `engine/extensions/amd-fsr1` | MIT. The extension build copies `AMD-FSR1-LICENSE.txt` beside the module. |

Vendored component READMEs record the original sources, versions and local
changes. The Fira Sans font is distributed unmodified with its original OFL and
copyright notice. The MIT option is selected for Nuklear and stb_image.

App bundles carry vendored notices under `Resources/licenses/` on macOS, or
`licenses/` next to the executable elsewhere. Exports also copy installed runtime
license and NOTICE files on macOS. SDL, Vulkan Loader and MoltenVK are separate
upstream projects. Export rewrites their library install paths and signs them
locally; it does not modify their implementation.

Compiler and shader tools are build dependencies, not bundled executables.
System Vulkan drivers and OS libraries remain supplied by the destination OS.
When changing dependencies or packaging on another platform, include the actual
runtime versions' licenses, notices and any additional required attributions.
