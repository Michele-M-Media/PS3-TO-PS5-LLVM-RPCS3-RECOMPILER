"""Inspect a PS3 disc dump or assemble a private local PS3 TO PS5 LLVM RPCS3 RECOMPILER folder."""

import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import sys

TEMPLATE = Path(__file__).resolve().parents[1] / "templates/rpcs3-recompiler.yml"
TITLE = "PS3 TO PS5 LLVM RPCS3 RECOMPILER"


def sfo(path):
    with path.open("rb") as file:
        data = file.read(128 * 1024 + 1)
    if len(data) < 20 or len(data) > 128 * 1024:
        raise ValueError("PARAM.SFO has an invalid size")
    magic, _version, keys, values, count = struct.unpack_from("<4s4I", data)
    if magic != b"\0PSF" or not 20 + 16 * count <= keys <= values <= len(data):
        raise ValueError("PARAM.SFO has invalid tables")
    result = {}
    for index in range(count):
        key_offset, fmt, length, reserved, offset = struct.unpack_from("<HHIII", data, 20 + index * 16)
        start = keys + key_offset
        end = data.find(b"\0", start, values)
        value = values + offset
        if end < 0 or length > reserved or value + length > len(data):
            raise ValueError("PARAM.SFO entry is out of bounds")
        if fmt == 0x0204:
            result[data[start:end].decode("ascii")] = data[value:value+length].split(b"\0", 1)[0].decode("utf-8")
    return result


def inspect(disc):
    game = disc / "PS3_GAME"
    boot = game / "USRDIR/EBOOT.BIN"
    if not (disc / "PS3_DISC.SFB").is_file() or not boot.is_file():
        raise ValueError("Select the folder containing PS3_DISC.SFB and PS3_GAME/USRDIR/EBOOT.BIN")
    fields = sfo(game / "PARAM.SFO")
    title_id = fields.get("TITLE_ID", "")
    if not re.fullmatch(r"[A-Z]{4}[0-9]{5}", title_id):
        raise ValueError("The disc does not specify a valid PS3 title ID")
    return {"title_id": title_id, "title": fields.get("TITLE", title_id), "app_version": fields.get("APP_VER")}


def native_metadata(path, display_mode="default"):
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    title_id = document.get("titleId", "")
    if not re.fullmatch(r"[A-Z]{4}[0-9]{5}", title_id) or title_id == "PPSA00000":
        raise ValueError("Use the param.json for your target homebrew title ID")
    localized = document.setdefault("localizedParameters", {})
    language = localized.setdefault("defaultLanguage", "en-US")
    localized.setdefault(language, {})
    for item in localized.values():
        if isinstance(item, dict):
            item["titleName"] = TITLE
    if display_mode != "default":
        attributes = document.get("attribute3", 0)
        if type(attributes) is not int or not 0 <= attributes <= 0xffffffff:
            raise ValueError("param.json must contain a numeric attribute3 bit field")
        document["attribute3"] = attributes | 0x40
        if display_mode == "120hz-vrr":
            document["attribute3"] |= 0x40000
    return document


def png(path):
    with path.open("rb") as file:
        signature = file.read(24)
    if len(signature) != 24 or signature[:8] != b"\x89PNG\r\n\x1a\n" or signature[12:16] != b"IHDR":
        raise ValueError(f"Invalid PNG: {path}")
    return path


def dds_background(path):
    with path.open("rb") as file:
        header = file.read(148)
    if (len(header) != 148 or header[:4] != b"DDS " or header[84:88] != b"DX10"
            or struct.unpack_from("<I", header, 4) != (124,)
            or struct.unpack_from("<II", header, 12) != (2160, 3840)
            or struct.unpack_from("<I", header, 28)[0] not in (0, 1)
            or struct.unpack_from("<IIII", header, 128) != (98, 3, 0, 1)
            or path.stat().st_size != 148 + 3840 * 2160):
        raise ValueError(f"Expected a single-level 3840x2160 BC7_UNORM DX10 DDS: {path}")
    return path


def stage(args):
    disc, output = args.disc.resolve(strict=True), args.output.resolve()
    info = inspect(disc)
    core = args.core.resolve(strict=True)
    if core.name.lower() != "eboot.bin" or not core.is_file():
        raise ValueError("Use the packaged PS3 TO PS5 LLVM RPCS3 RECOMPILER eboot.bin")
    firmware = args.firmware.resolve(strict=True)
    if not (firmware / "sys/external").is_dir():
        raise ValueError("Select the dev_flash directory from your local RPCS3 firmware installation")
    metadata = native_metadata(args.param, args.display_mode)
    icon = png(args.icon0 or TEMPLATE.with_name("icon0.png"))
    background = None
    if args.pic0:
        background = dds_background(args.pic0) if args.pic0.suffix.lower() == ".dds" else png(args.pic0)
    splash = dds_background(args.pic1) if args.pic1 else None
    if output.is_relative_to(disc) or output.is_relative_to(firmware):
        raise ValueError("Choose an output directory outside the input directories")
    # Reserve the destination once so retries cannot overwrite an existing dump.
    output.mkdir()
    shutil.copytree(disc / "PS3_GAME", output / "PS3_GAME")
    shutil.copyfile(disc / "PS3_DISC.SFB", output / "PS3_DISC.SFB")
    shutil.copytree(firmware, output / "dev_flash")
    shutil.copyfile(core, output / "eboot.bin")
    config = TEMPLATE.read_text(encoding="utf-8")
    if args.display_mode != "default":
        config = config.replace("  Renderer: Vulkan\n", "  Renderer: Vulkan\n  Frame limit: 120\n", 1)
    (output / TEMPLATE.name).write_text(config, encoding="utf-8", newline="\n")
    if args.spu_async:
        (output / "spu-async.enable").touch()
    elif args.spu_hybrid:
        (output / "spu-hybrid.enable").touch()
    (output / "sce_sys").mkdir()
    shutil.copyfile(icon, output / "sce_sys/icon0.png")
    if background:
        shutil.copyfile(background, output / ("sce_sys/pic0.dds" if background.suffix.lower() == ".dds" else "sce_sys/pic0.png"))
    if splash:
        shutil.copyfile(splash, output / "sce_sys/pic1.dds")
    modules = core.parent / "sce_module"
    if modules.is_dir():
        shutil.copytree(modules, output / "sce_module")
    (output / "sce_sys/param.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    check = subcommands.add_parser("inspect")
    check.add_argument("disc", type=Path)
    build = subcommands.add_parser("stage")
    for name in ("disc", "output", "core", "param", "firmware"):
        build.add_argument("--" + name, type=Path, required=True)
    build.add_argument("--icon0", type=Path, help="Override the default RPCS3 icon")
    build.add_argument("--pic0", type=Path, help="Use a 4K BC7 DDS selection background or a legacy PNG")
    build.add_argument("--pic1", type=Path, help="Use a 4K BC7 DDS launch image")
    build.add_argument("--display-mode", choices=("default", "120hz", "120hz-vrr"), default="default",
                       help="Select a high-refresh profile for a compatible game and display")
    spu = build.add_mutually_exclusive_group()
    spu.add_argument("--spu-hybrid", action="store_true", help="Allow AsmJit on live SPU cache misses")
    spu.add_argument("--spu-async", action="store_true", help="Run the SPU fast tier while LLVM compiles cold blocks")
    args = parser.parse_args()
    try:
        info = inspect(args.disc) if args.command == "inspect" else stage(args)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(info, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
