#!/usr/bin/env python3
"""Check the install package, including SFO identity and required runtime assets."""
import struct
import sys
import zipfile
import zlib
from pathlib import Path


LIVEAREA_IMAGES = {
    "sce_sys/icon0.png": (128, 128),
    "sce_sys/livearea/contents/bg.png": (840, 500),
    "sce_sys/livearea/contents/startup.png": (280, 158),
}


def check_livearea_png(data, name, dimensions):
    """Require the opaque indexed PNG encoding used by this app's LiveArea."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"Invalid PNG: {name}"
    offset = 8
    chunks = []
    while offset < len(data):
        assert offset + 12 <= len(data), f"Truncated PNG chunk: {name}"
        size, kind = struct.unpack_from(">I4s", data, offset)
        end = offset + 12 + size
        assert end <= len(data), f"Truncated PNG data: {name}"
        payload = data[offset + 8:end - 4]
        crc, = struct.unpack_from(">I", data, end - 4)
        assert zlib.crc32(kind + payload) == crc, f"Invalid PNG checksum: {name}"
        chunks.append((kind, payload))
        offset = end
    assert chunks and chunks[0][0] == b"IHDR", f"Missing PNG header: {name}"
    width, height, depth, color, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", chunks[0][1])
    assert (width, height) == dimensions, f"Wrong LiveArea dimensions: {name}"
    assert depth == 8 and color == 3, f"LiveArea requires 8-bit indexed PNG: {name}"
    assert (compression, filtering, interlace) == (0, 0, 0), f"Unsupported PNG encoding: {name}"
    palette = [payload for kind, payload in chunks if kind == b"PLTE"]
    assert len(palette) == 1 and 0 < len(palette[0]) <= 768 and len(palette[0]) % 3 == 0, f"Invalid PNG palette: {name}"
    # All three shipped assets are opaque. In particular icon0/bg must not have alpha.
    assert not any(kind == b"tRNS" for kind, _ in chunks), f"Unexpected LiveArea alpha: {name}"
    assert any(kind == b"IDAT" for kind, _ in chunks), f"Missing PNG pixels: {name}"
    assert chunks[-1] == (b"IEND", b""), f"Missing PNG end: {name}"


def check_vpk(path):
    with zipfile.ZipFile(path) as vpk:
        assert vpk.testzip() is None, "Corrupt VPK entry"
        check_contents(vpk)


def check_contents(vpk):
    required = ["eboot.bin", "sce_sys/param.sfo", "sce_sys/icon0.png",
                "sce_sys/livearea/contents/template.xml", "sce_sys/livearea/contents/bg.png",
                "sce_sys/livearea/contents/startup.png", "resources/font/switch_font.ttf",
                "resources/cacert.pem", "resources/MOONFIN_NOTICES.txt"]
    for name in required:
        assert name in vpk.namelist(), f"Missing VPK entry: {name}"
        assert vpk.getinfo(name).file_size > 0, f"Empty VPK entry: {name}"
    for name, dimensions in LIVEAREA_IMAGES.items():
        check_livearea_png(vpk.read(name), name, dimensions)
    sfo = vpk.read("sce_sys/param.sfo")
    magic, version, keys, values, count = struct.unpack_from("<5I", sfo)
    assert magic == 0x46535000
    fields = {}
    for i in range(count):
        key_offset, fmt, size, max_size, data_offset = struct.unpack_from("<HHIII", sfo, 20 + 16 * i)
        key = sfo[keys + key_offset:].split(b"\0", 1)[0].decode()
        fields[key] = sfo[values + data_offset:values + data_offset + size].rstrip(b"\0")
    assert fields["TITLE_ID"] == b"MFVITA001", fields
    assert fields["TITLE"] == b"Moonfin Vita", fields
    assert vpk.read("resources/cacert.pem").count(b"BEGIN CERTIFICATE") > 50
    print("VPK contents, LiveArea PNG encoding and application identity verified:", len(vpk.namelist()), "entries")


def check_thread_stack(path):
    """Check the linked ELF, not just source text: pthread must see our override."""
    data = Path(path).read_bytes()
    assert data[:6] == b"\x7fELF\x01\x01", "Expected a little-endian ELF32 executable"
    section_offset, = struct.unpack_from("<I", data, 32)
    section_size, section_count = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<10I", data, section_offset + i * section_size)
                for i in range(section_count)]
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, address, size, info, other, index = struct.unpack_from("<IIIBBH", data, offset)
            if names[name:].split(b"\0", 1)[0] != b"_pthread_stack_default_user":
                continue
            assert info >> 4 == 1 and info & 15 == 1 and size == 4, "Thread stack override must be a strong object symbol"
            assert 0 < index < len(sections), "Thread stack override is undefined"
            storage = sections[index]
            value, = struct.unpack_from("<I", data, storage[4] + address - storage[3])
            assert value >= 2 * 1024 * 1024, "Thread stack is too small for Vita FFmpeg networking"
            print("Linked pthread default stack verified:", value, "bytes")
            return
    raise AssertionError("Missing linked _pthread_stack_default_user override")


if __name__ == "__main__":
    check_vpk(sys.argv[1])
    if len(sys.argv) > 2:
        check_thread_stack(sys.argv[2])
