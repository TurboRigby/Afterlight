# SPDX-License-Identifier: MPL-2.0
"""Build the HDR, low-resolution motion vector Vulkan permutations used by Afterlight."""

import json
import re
import struct
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

PASSES = {
    "fsr2": {
        "DEPTH_CLIP": "depth_clip",
        "RECONSTRUCT_PREVIOUS_DEPTH": "reconstruct_previous_depth",
        "LOCK": "lock",
        "ACCUMULATE": "accumulate",
        "ACCUMULATE_SHARPEN": "accumulate",
        "RCAS": "rcas",
        "COMPUTE_LUMINANCE_PYRAMID": "compute_luminance_pyramid",
        "GENERATE_REACTIVE": "autogen_reactive",
        "TCR_AUTOGENERATE": "tcr_autogen",
    },
    "fsr3upscaler": {
        "PREPARE_INPUTS": "prepare_inputs",
        "PREPARE_REACTIVITY": "prepare_reactivity",
        "SHADING_CHANGE": "shading_change",
        "ACCUMULATE": "accumulate",
        "ACCUMULATE_SHARPEN": "accumulate",
        "RCAS": "rcas",
        "LUMA_PYRAMID": "luma_pyramid",
        "GENERATE_REACTIVE": "autogen_reactive",
        "SHADING_CHANGE_PYRAMID": "shading_change_pyramid",
        "LUMA_INSTABILITY": "luma_instability",
        "DEBUG_VIEW": "debug_view",
    },
}

# FfxShaderBlob expects these seven classes in this order.
CLASSES = ("cb", "srvTexture", "uavTexture", "srvBuffer", "uavBuffer", "sampler", "acceleration")

# Match the formats allocated by the SDK 1.1.4 host components.
STORAGE_FORMATS = {
    "fsr2": {
        "FSR2_BIND_UAV_DILATED_DEPTH": ("r16f", "r32f"),
        "FSR2_BIND_UAV_PREPARED_INPUT_COLOR": ("rgba16", "rgba16f"),
    },
    "fsr3upscaler": {
        "FSR3UPSCALER_BIND_UAV_LUMA_HISTORY": ("rgba8", "rgba16f"),
    },
}


def descriptor_bindings(assembly):
    names = dict(re.findall(r'OpName (%\d+) "([^"]+)"', assembly))
    sets = dict(re.findall(r"OpDecorate (%\d+) DescriptorSet (\d+)", assembly))
    variables = dict(re.findall(r"(%\d+) = OpVariable (%\d+) ", assembly))
    pointers = dict(re.findall(r"(%\d+) = OpTypePointer \w+ (%\d+)", assembly))
    arrays = dict((type_id, (element, length)) for type_id, element, length
                  in re.findall(r"(%\d+) = OpTypeArray (%\d+) (%\d+)", assembly))
    constants = dict(re.findall(r"(%\d+) = OpConstant %\d+ (\d+)", assembly))
    resources = {category: [] for category in CLASSES}
    for variable, binding in re.findall(r"OpDecorate (%\d+) Binding (\d+)", assembly):
        name = names[variable]
        category = next((category for prefix, category in
                         (("cb", "cb"), ("r_", "srvTexture"), ("rw_", "uavTexture"), ("s_", "sampler"))
                         if name.startswith(prefix)), None)
        if not category or sets[variable] != "0":
            raise ValueError(f"Unexpected FidelityFX shader binding {name} at set {sets[variable]}")
        type_id = pointers[variables[variable]]
        count = int(constants[arrays[type_id][1]]) if type_id in arrays else 1
        resources[category].append((int(binding), name, count))
    for bindings in resources.values():
        bindings.sort()
    return resources


def emit_blob(out, name, code, resources):
    words = struct.unpack(f"<{len(code) // 4}I", code)
    out.write(f"static const uint32_t {name}_code[] = {{\n")
    for offset in range(0, len(words), 8):
        out.write("    " + ", ".join(f"0x{word:08x}" for word in words[offset:offset + 8]) + ",\n")
    out.write("};\n")
    fields = []
    for category, bindings in resources.items():
        if not bindings:
            fields.extend(["nullptr"] * 4)
            continue
        for suffix, field_type, values in (
            ("names", "const char*", [json.dumps(item[1]) for item in bindings]),
            ("bindings", "const uint32_t", [str(item[0]) for item in bindings]),
            ("counts", "const uint32_t", [str(item[2]) for item in bindings]),
            ("sets", "const uint32_t", ["0"] * len(bindings)),
        ):
            field = f"{name}_{category}_{suffix}"
            out.write(f"static {field_type} {field}[] = {{ {', '.join(values)} }};\n")
            fields.append(field)
    counts = ", ".join(str(len(bindings)) for bindings in resources.values())
    out.write(f"static const FfxShaderBlob {name} = {{\n"
              f"    reinterpret_cast<const uint8_t*>({name}_code), sizeof({name}_code),\n"
              f"    {counts},\n    {', '.join(fields)}\n}};\n")


def compile_shaders(component, sdk, glslang, disassemble, output):
    options = f"FFX_{component.upper()}_OPTION_"
    gpu = sdk / "include/FidelityFX/gpu"
    source_dir = sdk / "src/backends/vk/shaders" / component
    prefix = f"FFX_{component.upper()}"
    with TemporaryDirectory() as temporary, output.open("w", encoding="utf-8") as out:
        overlay = Path(temporary)
        header_name = f"ffx_{component}_callbacks_glsl.h"
        header = (gpu / component / header_name).read_text(encoding="utf-8")
        for binding, (old, new) in STORAGE_FORMATS[component].items():
            header, count = re.subn(rf"(binding\s*=\s*{binding}\s*,\s*){old}\b",
                                    rf"\g<1>{new}", header)
            if count != 1:
                raise ValueError(f"Expected one SDK 1.1.4 storage format declaration for {binding}")
        (overlay / component).mkdir()
        (overlay / component / header_name).write_text(header, encoding="utf-8")
        out.write("// Generated from the MIT-licensed AMD FidelityFX SDK.\n"
                  "#include <cstring>\n#include <FidelityFX/host/ffx_" + component + ".h>\n"
                  "#include \"ffx_shader_blobs.h\"\n")
        spirv = Path(temporary) / "pass.spv"
        for pass_name, source in PASSES[component].items():
            definitions = {
                "HDR_COLOR_INPUT": 1,
                "LOW_RESOLUTION_MOTION_VECTORS": 1,
                "JITTERED_MOTION_VECTORS": 0,
                "INVERTED_DEPTH": 0,
                "APPLY_SHARPENING": int(pass_name == "ACCUMULATE_SHARPEN"),
                "REPROJECT_USE_LANCZOS_TYPE": 0,
                "UPSAMPLE_USE_LANCZOS_TYPE": 2,
                "UPSAMPLE_SAMPLERS_USE_DATA_HALF": 0,
                "ACCUMULATE_SAMPLERS_USE_DATA_HALF": 0,
                "REPROJECT_SAMPLERS_USE_DATA_HALF": 1,
                "POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF": 0,
            }
            shader = source_dir / f"ffx_{component}_{source}_pass.glsl"
            subprocess.run([glslang, "--target-env", "vulkan1.2", "-S", "comp", "-Os",
                            f"-I{overlay}", f"-I{gpu}", f"-I{gpu / component}",
                            "-DFFX_GPU=1", "-DFFX_GLSL=1", "-DFFX_HALF=0",
                            *[f"-D{options}{key}={value}" for key, value in definitions.items()],
                            str(shader), "-o", str(spirv)], check=True)
            assembly = subprocess.check_output([disassemble, "--raw-id", str(spirv)], text=True)
            emit_blob(out, f"blob_{pass_name}", spirv.read_bytes(), descriptor_bindings(assembly))
        out.write("FfxErrorCode ffxGetPermutationBlobByIndex(FfxEffect effect, FfxPass pass,\n"
                  "    FfxBindStage, uint32_t, FfxShaderBlob* out) {\n"
                  f"    if (effect != FFX_EFFECT_{component.upper()}) return FFX_ERROR_INVALID_ENUM;\n"
                  "    const FfxShaderBlob* blob;\n    switch (pass) {\n")
        for pass_name in PASSES[component]:
            out.write(f"    case {prefix}_PASS_{pass_name}: blob = &blob_{pass_name}; break;\n")
        out.write("    default: return FFX_ERROR_INVALID_ENUM;\n    }\n"
                  "    std::memcpy(out, blob, sizeof(*out));\n    return FFX_OK;\n}\n"
                  "FfxErrorCode ffxIsWave64(FfxEffect, uint32_t, bool& wave64) {\n"
                  "    wave64 = false;\n    return FFX_OK;\n}\n")


if __name__ == "__main__":
    component, sdk, glslang, disassemble, output = sys.argv[1:]
    compile_shaders(component, Path(sdk), glslang, disassemble, Path(output))
