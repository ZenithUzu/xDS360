#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Rewrites the file lists of xdk/xds360.vcxproj and .filters from what is on
# disk under xdk/, so the VS2010 project follows convert_noods.py output.
import os
import re
import sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "xdk")
sources, headers = [], []
for folder in ("compat", "frontend", "noods"):
    for base, dirs, files in os.walk(os.path.join(root, folder)):
        dirs.sort()
        for name in sorted(files):
            rel = os.path.relpath(os.path.join(base, name), root).replace("/", "\\")
            if name.endswith(".cpp"):
                sources.append(rel)
            elif name.endswith(".h") or "." not in name:  # compat has <thread>, <mutex>...
                headers.append(rel)

def filter_of(path):
    return os.path.dirname(path)

def write(path, text):
    with open(path, "w", newline="\r\n", encoding="utf-8-sig") as f:
        f.write(text)

# vcxproj: replace every ItemGroup holding ClCompile/ClInclude with one fresh group.
proj_path = os.path.join(root, "xds360.vcxproj")
proj = open(proj_path, encoding="utf-8-sig").read().replace("\r\n", "\n")
proj = re.sub(r"  <ItemGroup>\n(    <Cl(Compile|Include) [^\n]*\n)+  </ItemGroup>\n", "", proj)
items = "".join('    <ClCompile Include="%s" />\n' % s for s in sources)
items += "".join('    <ClInclude Include="%s" />\n' % h for h in headers)
proj = proj.replace('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
                    "  <ItemGroup>\n" + items + "  </ItemGroup>\n"
                    '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
proj = proj.replace("compat;noods;frontend;", "compat;noods\\core;frontend;")
if "xaudio2.lib" not in proj:
    for lib in ("xapilibd.lib;", "xapilib.lib;"):
        proj = proj.replace(lib, lib + "xaudio2.lib;xmcore.lib;")
write(proj_path, proj)

# filters: one filter per folder so Solution Explorer mirrors the tree.
folders = set()
for p in sources + headers:
    d = filter_of(p)
    while d:
        folders.add(d)
        d = os.path.dirname(d)
out = ['<?xml version="1.0" encoding="utf-8"?>',
       '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">',
       "  <ItemGroup>"]
for i, d in enumerate(sorted(folders)):
    out.append('    <Filter Include="%s">' % d)
    out.append("      <UniqueIdentifier>{9e0c%04x-1d5a-4c3e-9b1a-0000000000%02x}</UniqueIdentifier>" % (i, i % 256))
    out.append("    </Filter>")
out.append("  </ItemGroup>")
out.append("  <ItemGroup>")
for kind, files in (("ClCompile", sources), ("ClInclude", headers)):
    for p in files:
        out.append('    <%s Include="%s">' % (kind, p))
        out.append("      <Filter>%s</Filter>" % filter_of(p))
        out.append("    </%s>" % kind)
out.append("  </ItemGroup>")
out.append("</Project>")
write(proj_path + ".filters", "\n".join(out) + "\n")
print("%d sources, %d headers" % (len(sources), len(headers)), file=sys.stderr)
