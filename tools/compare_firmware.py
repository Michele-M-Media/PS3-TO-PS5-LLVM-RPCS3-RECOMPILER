"""Compare loader-visible requirements of two converted PS5 application ELFs."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def encoded_id(value):
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"
    if value < 64:
        return alphabet[value]
    return encoded_id(value >> 6) + alphabet[value & 63]


def requirements(path):
    """Read only the ELF layout emitted by the native application converter."""
    with path.open("rb") as stream:
        length = path.stat().st_size

        def read(offset, size):
            if offset < 0 or size < 0 or offset + size > length or size > 16 * 1024**2:
                raise ValueError("Invalid ELF table range")
            stream.seek(offset)
            data = stream.read(size)
            if len(data) != size:
                raise ValueError("Incomplete ELF table")
            return data

        header = struct.unpack("<16sHHIQQQIHHHHHH", read(0, 64))
        if header[0][:6] != b"\x7fELF\x02\x01" or header[2] != 62 or header[9] != 56:
            raise ValueError("Expected a converted x86-64 ELF64 application")
        segments = list(struct.iter_unpack("<IIQQQQQQ", read(header[5], header[10] * 56)))
        dynamic, = (s for s in segments if s[0] == 2)
        tags = []
        for tag, value in struct.iter_unpack("<QQ", read(dynamic[2], dynamic[5])):
            if tag == 0:
                break
            tags.append((tag, value))
        else:
            raise ValueError("Unterminated dynamic table")

        def mapped(address, size):
            for kind, _, offset, base, _, file_size, _, _ in segments:
                if kind == 1 and base <= address and address + size <= base + file_size:
                    return read(offset + address - base, size)
            raise ValueError("ELF table outside a load segment")

        table = dict(tags)
        strings = mapped(table[5], table[10])
        if table[11] != 24:
            raise ValueError("Unsupported symbol size")
        symbols = mapped(table[6], table[0x6100003F])

    def string(offset):
        end = strings.index(b"\0", offset)
        return strings[offset:end].decode("ascii")

    def declarations(kind):
        return {encoded_id(v >> 48): (string(v & 0xffffffff), (v >> 32) & 0xffff)
                for tag, v in tags if tag == kind}

    modules = declarations(0x61000045)
    libraries = declarations(0x61000049)
    needed = {string(v) for tag, v in tags if tag == 1}
    imports = set()
    for offset, info, _, section, _, _ in struct.iter_unpack("<IBBHQQ", symbols):
        if section or not offset or info >> 4 not in (1, 2):
            continue
        nid, lib, module = string(offset).split("#")
        imports.add((*modules[module], *libraries[lib], nid, info >> 4))
    if not imports or not needed:
        raise ValueError("Expected native imports and needed modules")
    return imports, needed


def inspect_sample(root, needed):
    metadata = root / "system_ex/etc/index.dat"
    match = re.search(rb"(?m)^release:\s*(0x[0-9a-fA-F]{8})\s*$", metadata.read_bytes())
    if not match:
        raise ValueError("Missing firmware release in index.dat")
    directory = root / "system/common/lib"
    if not directory.is_dir():
        raise ValueError("Missing system/common/lib in firmware sample")
    files = []
    for name in sorted(needed):
        if not re.fullmatch(r"[A-Za-z0-9_+-]+\.prx", name):
            raise ValueError(f"Unexpected loader module name {name!r}")
        provider = directory / (Path(name).stem + ".sprx")
        entry = {"needed": name, "same_stem_file": provider.is_file()}
        if provider.is_file():
            with provider.open("rb") as stream:
                magic = stream.read(4)
            entry.update(file=provider.name, bytes=provider.stat().st_size, sha256=sha256(provider),
                         format={b"\x7fELF": "ELF", bytes.fromhex("5414f5ee"): "SELF"}.get(magic, "unknown"))
        files.append(entry)
    return {"release": match[1].decode(), "index_sha256": sha256(metadata), "providers": files,
            "scope": "Module filenames and contents hashed. Export resolution and runtime behavior require separate evidence."}


def compare(candidate, baseline, firmware):
    current, needed = requirements(candidate)
    previous, prior_needed = requirements(baseline)
    added, removed = sorted(current - previous), sorted(previous - current)
    added_required = [r for r in added if r[-1] == 1]
    return {"candidate_sha256": sha256(candidate), "baseline_sha256": sha256(baseline),
            "strong": sum(row[-1] == 1 for row in current), "weak": sum(row[-1] == 2 for row in current),
            "by_module": dict(sorted(Counter(row[0] for row in current).items())),
            "import_fields": ["module", "module_version", "library", "library_version", "nid", "binding"],
            "added": added, "removed": removed, "added_required": added_required, "needed_added": sorted(needed - prior_needed),
            "needed_removed": sorted(prior_needed - needed), "firmware_sample": inspect_sample(firmware, needed),
            "hardware_tested": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("candidate", "baseline", "firmware"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        result = compare(args.candidate, args.baseline, args.firmware)
    except (ValueError, OSError, KeyError, struct.error) as error:
        parser.exit(2, f"Cannot compare native requirements: {error}\n")
    print(json.dumps(result, indent=2))
    raise SystemExit(1 if result["added_required"] or result["needed_added"] else 0)
