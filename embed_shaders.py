import os
import re
import sys
import subprocess
from pathlib import Path

# Regex to detect [shader("stage")] entrypoints
ENTRY_REGEX = re.compile(
    r'\[shader\("(\w+)"\)\](?:\s*\[[^\]]+\])*\s+([^\s(]+)\s+(\w+)\s*\(',
    re.MULTILINE
)

STAGE_SUFFIX = {
    "vertex": "vert",
    "fragment": "frag",
    "compute": "comp",
    "geometry": "geom",
    "hull": "hull",
    "domain": "dom",
    "raygeneration": "rgen",
    "anyhit": "ahit",
    "closesthit": "chit",
    "miss": "miss",
    "intersection": "isec",
    "callable": "call"
}

def find_entry_points(source_path: Path):
    text = source_path.read_text(encoding='utf-8')
    matches = ENTRY_REGEX.findall(text)
    return [(stage, func) for stage, _, func in matches]


def compile_shader(src: Path, entry: str, stage: str, out_path: Path):
    cmd = [
        "slangc",
        str(src),
        "-entry", entry,
        "-stage", stage,
        "-target", "spirv",
        "-matrix-layout-column-major",
        "-O2",
        "-fvk-use-entrypoint-name",
        "-o", str(out_path),
    ]
    subprocess.check_call(cmd)


def format_as_cpp_array(data: bytes, array_name: str, indent: int = 4):
    words = [int.from_bytes(data[i:i+4], "little")
             for i in range(0, len(data), 4)]

    lines, line = [], []
    for i, word in enumerate(words):
        line.append(f"0x{word:08X}")
        if (i + 1) % 8 == 0:
            lines.append(", ".join(line))
            line = []
    if line:
        lines.append(", ".join(line))

    joined = ",\n".join(" " * indent + l for l in lines)
    return f"inline constexpr uint32_t {array_name}[] = {{\n{joined}\n}};\n"


def main():
    if len(sys.argv) != 3:
        print("Usage: embed_shaders.py <input_shader_dir> <output_header>")
        sys.exit(1)

    shader_dir = Path(sys.argv[1]).resolve()
    output_header = Path(sys.argv[2]).resolve()

    bin_dir = shader_dir / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)

    slang_files = list(shader_dir.rglob("*.slang"))

    out_lines = []
    out_lines.append("// This file is auto-generated. Do not touch.")
    out_lines.append("#pragma once")
    out_lines.append("#include <stdint.h>")
    out_lines.append("#include \"../shaders.h\"\n")

    for slang_file in slang_files:
        rel_parts = slang_file.relative_to(shader_dir).with_suffix("").parts
        ns_parts = ["shaders"] + list(rel_parts[:-1])
        fname = slang_file.stem

        entries = find_entry_points(slang_file)
        if not entries:
            continue

        for ns in ns_parts:
            out_lines.append(f"namespace {ns} {{")

        for stage, entry in entries:
            suffix = STAGE_SUFFIX.get(stage.lower(), stage.lower())
            array_name = f"{fname}_{entry}_{suffix}_spv"
            spv_file = bin_dir / f"{fname}_{entry}_{suffix}.spv"
            compile_shader(slang_file, entry, stage, spv_file)
            data = spv_file.read_bytes()
            out_lines.append(format_as_cpp_array(data, array_name))
            out_lines.append(f"inline constexpr CompiledSpirvShader {fname}_{entry}_{suffix} {{ \"{entry}\", &{array_name}[0], sizeof({array_name}) }};\n")

        for ns in reversed(ns_parts):
            out_lines.append(f"}} // namespace {ns}")

        out_lines.append("")

    output_header.write_text("\n".join(out_lines))

if __name__ == "__main__":
    main()

