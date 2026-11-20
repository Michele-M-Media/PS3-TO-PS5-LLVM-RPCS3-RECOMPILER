"""Pack a locally installed PS3 disc update and generate its launcher manifest."""

import argparse
import hashlib
from pathlib import Path
import re
from tempfile import TemporaryDirectory

from prepare_dump import inspect, sfo

BOOT_FILES = ("USRDIR/EBOOT.BIN", "PARAM.SFO")
ARCHIVE = "UPDATE.BIN"
MANIFEST = "recompiler_title_update_manifest.h"


def literal(value):
    return '"' + ''.join(chr(byte) if 32 <= byte < 127 and byte not in (34, 92)
                         else f"\\{byte:03o}" for byte in value.encode("utf-8")) + '"'


def initializer(name, offset, size, digest):
    sha = ", ".join(f"0x{value:02x}" for value in digest)
    return f"{{{literal(name)}, {offset}, {size}, {{{sha}}}}}"


def pack(disc, update, output):
    title_id = inspect(disc)["title_id"]
    files = {}
    for path in update.rglob("*"):
        relative = path.relative_to(update).as_posix()
        if path.is_symlink() or path.is_junction() or any(c in relative for c in '\\:\r\n'):
            raise ValueError(f"Unsupported update path: {relative}")
        if path.is_file():
            files[relative] = path
        elif not path.is_dir():
            raise ValueError(f"Unsupported file type: {relative}")
    if not all(name in files for name in BOOT_FILES) or files[BOOT_FILES[0]].stat().st_size == 0:
        raise ValueError("The installed update must contain its executable and PARAM.SFO")
    fields = sfo(files["PARAM.SFO"])
    version = fields.get("APP_VER", "")
    if fields.get("TITLE_ID") != title_id or fields.get("CATEGORY") != "GD" or not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", version):
        raise ValueError("The update must be game data for the exact disc title ID and a valid APP_VER")
    if output.exists() or output.is_relative_to(update) or output.is_relative_to(disc):
        raise ValueError("Choose a new output folder outside the disc and update folders")

    order = sorted(files.keys() - set(BOOT_FILES)) + list(BOOT_FILES)
    with TemporaryDirectory(dir=output.parent) as scratch:
        pending = Path(scratch) / "packed"
        pending.mkdir()
        archive_hash = hashlib.sha256()
        rows = []
        with (pending / ARCHIVE).open("xb") as bundle:
            for name in order:
                start = bundle.tell()
                file_hash = hashlib.sha256()
                with files[name].open("rb") as source:
                    while chunk := source.read(65536):
                        file_hash.update(chunk)
                        archive_hash.update(chunk)
                        bundle.write(chunk)
                rows.append("    " + initializer(name, start, bundle.tell() - start, file_hash.digest()) + ",")
            archive = initializer(ARCHIVE, 0, bundle.tell(), archive_hash.digest())
        (pending / MANIFEST).write_text("\n".join([
            "#pragma once", "", "#include <array>", "#include <cstdint>", "",
            "namespace recompiler_title_update", "{",
            f'inline constexpr const char title_id[] = "{title_id}";',
            f'inline constexpr const char update_version[] = "{version}";',
            "struct asset { const char* path; std::uint64_t offset, size; std::array<unsigned char, 32> hash; };",
            f"inline constexpr std::array<asset, {len(rows)}> assets{{{{",
            *rows, "}};", f"inline constexpr asset archive{archive};", "}", "",
        ]), encoding="utf-8", newline="\n")
        pending.rename(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("disc", "update", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        pack(args.disc.resolve(strict=True), args.update.resolve(strict=True), args.output.resolve())
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print(args.output / ARCHIVE)
    print(args.output / MANIFEST)


if __name__ == "__main__":
    main()
