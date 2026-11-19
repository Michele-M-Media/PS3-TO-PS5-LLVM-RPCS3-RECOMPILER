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


def native_metadata(path):
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
    return document


def png(path):
    with path.open("rb") as file:
        signature = file.read(24)
    if len(signature) != 24 or signature[:8] != b"\x89PNG\r\n\x1a\n" or signature[12:16] != b"IHDR":
        raise ValueError(f"Invalid PNG: {path}")
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
    metadata = native_metadata(args.param)
    icon = png(args.icon0 or TEMPLATE.with_name("icon0.png"))
    background = png(args.pic0) if args.pic0 else None
    if output.is_relative_to(disc) or output.is_relative_to(firmware):
        raise ValueError("Choose an output directory outside the input directories")
    # Reserve the destination once so retries cannot overwrite an existing dump.
    output.mkdir()
    shutil.copytree(disc / "PS3_GAME", output / "PS3_GAME")
    shutil.copyfile(disc / "PS3_DISC.SFB", output / "PS3_DISC.SFB")
    shutil.copytree(firmware, output / "dev_flash")
    shutil.copyfile(core, output / "eboot.bin")
    shutil.copyfile(TEMPLATE, output / TEMPLATE.name)
    (output / "sce_sys").mkdir()
    shutil.copyfile(icon, output / "sce_sys/icon0.png")
    if background:
        shutil.copyfile(background, output / "sce_sys/pic0.png")
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
    build.add_argument("--pic0", type=Path, help="Use a custom background")
    args = parser.parse_args()
    try:
        info = inspect(args.disc) if args.command == "inspect" else stage(args)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(info, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
