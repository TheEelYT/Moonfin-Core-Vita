#!/usr/bin/env python3
"""Check the install package, including SFO identity and required runtime assets."""
import struct
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1]) as vpk:
    required = ["eboot.bin", "sce_sys/param.sfo", "sce_sys/icon0.png",
                "sce_sys/livearea/contents/template.xml", "sce_sys/livearea/contents/bg.png",
                "sce_sys/livearea/contents/startup.png", "resources/font/switch_font.ttf",
                "resources/cacert.pem", "resources/MOONFIN_NOTICES.txt"]
    for name in required:
        assert name in vpk.namelist(), f"Missing VPK entry: {name}"
        assert vpk.getinfo(name).file_size > 0, f"Empty VPK entry: {name}"
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
    print("VPK contents and application identity verified:", len(vpk.namelist()), "entries")
