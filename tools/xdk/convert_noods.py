#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert the pinned, Xbox-patched NooDS core and its console UI into C++
that VS2010 accepts.

Input:  third_party/noods/src (after scripts/fetch_noods.sh)
Output: xdk/noods/core and xdk/noods/ui (committed, so the Windows build needs
        no scripts). The UI's BMP images become C arrays in ui/console/images.cpp.

VS2010 has no in-class member initializers (`int x = 0;` inside a class).
Each one is moved into the member-initializer list of the class constructor
(`Foo(): x(0) {}`), which C++ runs in declaration order, exactly like the
original. Structs without a constructor get a default one. Array members with
an explicit value list are value-initialized and then filled in the
constructor body. A few remaining incompatibilities are fixed by exact text
replacements that fail loudly if upstream changes.

Requires: pip install libclang
"""
import os
import re
import shutil
import sys

import clang.cindex as ci

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(REPO, 'third_party', 'noods', 'src')
OUT = os.path.join(REPO, 'xdk', 'noods')
SOURCES_MK = os.path.join(REPO, 'platform', 'xenon', 'noods_sources.mk')
RESOURCE_DIR = os.popen('clang -print-resource-dir').read().strip()
CLANG_ARGS = ['-resource-dir', RESOURCE_DIR, '-x', 'c++', '-std=c++11', '-DNOODS_XENON=1', '-DLOG_LEVEL=0',
              '-DENDIAN_BIG=1', '-DNO_FDOPEN=1', '-include', 'cstdint', '-I' + os.path.join(SRC, 'core')]
# The console UI (file browser, menus, screen layouts) used by NooDS on Wii U.
UI_SOURCES = ['ui/screen_layout.cpp', 'ui/nds_icon.cpp', 'ui/console/console_ui.cpp']
UI_FILES = UI_SOURCES + ['ui/screen_layout.h', 'ui/nds_icon.h', 'ui/console/console_ui.h']
UI_IMAGES = ['file_dark', 'file_light', 'folder_dark', 'folder_light', 'font']

VECTOR_LIST = re.compile(r'((?:const )?)std::vector<([\w:]+)> (\w+) = \{(.*?)\};', re.S)
VECTOR_ARRAY = (r'\1\2 \3Items[] = {\4};\n    \1std::vector<\2> \3(\3Items, '
                r'\3Items + sizeof(\3Items) / sizeof(\3Items[0]));')

# Text fixes that the member-initializer pass cannot express. Each pattern must
# match exactly the given number of times.
TEXT_FIXES = [
    # GCC builtins and attributes -> portable helpers from xds_compat.h.
    ('core/defines.h', '#include <cstdio>\n', '#include <cstdint>\n#include <cstdio>\n#include "xds_compat.h"\n', 1),
    ('core/defines.h', '__builtin_memcpy', 'memcpy', 4),
    ('core/defines.h', '__builtin_bswap16', 'XDS_BSWAP16', 3),
    ('core/defines.h', '__builtin_bswap32', 'XDS_BSWAP32', 4),
    ('core/defines.h', '__builtin_bswap64', 'XDS_BSWAP64', 1),
    ('core/defines.h', 'typedef uint16_t AliasUint16 __attribute__((may_alias));',
     'typedef uint16_t AliasUint16;', 1),
    ('core/defines.h', 'typedef uint32_t AliasUint32 __attribute__((may_alias));',
     'typedef uint32_t AliasUint32;', 1),
    ('core/defines.h', '#include <unistd.h>', '#include "xds_unistd.h"', 1),
    # VS2010 only has std::to_string(long long / unsigned long long / long double).
    ('core/settings.cpp', 'std::to_string(*(int*)settings[i].value)',
     'std::to_string((long long)*(int*)settings[i].value)', 1),
    ('core/memory/cartridge.cpp', 'std::to_string(core->id + 1)',
     'std::to_string((long long)(core->id + 1))', 2),
    ('core/settings.cpp', '#include <sys/stat.h>', '#include "xds_unistd.h"', 1),
    # Assigning an empty braced list to a container is C++11-only.
    ('core/io/aes.cpp', 'writeFifo = {};', 'writeFifo.clear();', 1),
    ('core/io/aes.cpp', 'readFifo = {};', 'readFifo.clear();', 1),
    ('core/memory/sd_mmc.cpp', 'dataFifo32 = {};', 'dataFifo32.clear();', 1),
    # VS2010's math.h has float/double/long double overloads, so sqrt(integer)
    # is ambiguous. GCC converts the integer to double; do the same explicitly.
    ('core/hle/hle_bios.cpp', '*registers[0] = sqrt(*registers[0]);',
     '*registers[0] = sqrt((double)*registers[0]);', 1),
    ('core/io/div_sqrt.cpp', 'sqrtResult = sqrt((uint32_t)sqrtParam);',
     'sqrtResult = sqrt((double)(uint32_t)sqrtParam);', 1),
    # A static std::vector filled from a braced list -> fill it from an array.
    ('core/settings.cpp', re.compile(r'std::vector<Setting> Settings::settings = \{(.*?)\n\};\n', re.S),
     lambda m: ('static Setting xdsSettingsInit[] = {%s\n};\nstd::vector<Setting> Settings::settings(xdsSettingsInit,\n'
                '    xdsSettingsInit + sizeof(xdsSettingsInit) / sizeof(xdsSettingsInit[0]));\n'
                % re.sub(r'&(\w+),', r'&Settings::\1,', m.group(1))), 1),
    # Arrays of objects without a default constructor -> XdsArray (built in place).
    ('core/core.h', '    Dma dma[2];', '    XdsArray<Dma, 2> dma;', 1),
    ('core/core.h', '    Gpu2D gpu2D[2];', '    XdsArray<Gpu2D, 2> gpu2D;', 1),
    ('core/core.h', '    HleBios hleBios[3];', '    XdsArray<HleBios, 3> hleBios;', 1),
    ('core/core.h', '    Interpreter interpreter[2];', '    XdsArray<Interpreter, 2> interpreter;', 1),
    ('core/core.h', '    Ndma ndma[2];', '    XdsArray<Ndma, 2> ndma;', 1),
    ('core/core.h', '    Timers timers[2];', '    XdsArray<Timers, 2> timers;', 1),
    ('core/core.cpp', re.compile(r'dma \{ Dma\(this, 0\), Dma\(this, 1\) \}'), 'dma(this)', 1),
    ('core/core.cpp', re.compile(r'gpu2D \{ Gpu2D\(this, 0\), Gpu2D\(this, 1\) \}'), 'gpu2D(this)', 1),
    ('core/core.cpp', re.compile(r'interpreter \{ Interpreter\(\s*this, 0\), Interpreter\(this, 1\) \}'),
     'interpreter(this)', 1),
    ('core/core.cpp', re.compile(r'ndma \{ Ndma\(this, 0\), Ndma\(this, 1\) \}'), 'ndma(this)', 1),
    ('core/core.cpp', re.compile(r'timers \{ Timers\(this, 0\), Timers\(this, 1\) \}'), 'timers(this)', 1),
    ('core/core.cpp', re.compile(r'hleBios \{ (HleBios\(this, 0, HleBios::swiTable9\)), HleBios\(this, 1, HleBios::\s*'
                            r'swiTable7\), (HleBios\(this, 1, HleBios::swiTableGba\)) \}'),
     r'hleBios(\1, HleBios(this, 1, HleBios::swiTable7), \2)', 1),
    # Console UI.
    # VS2010 has no delegating constructors; the other one takes a const char* too.
    ('ui/nds_icon.h', '    NdsIcon(const char *path): NdsIcon(path, -1) {}\n', '', 1),
    ('ui/console/console_ui.cpp', '#include <dirent.h>\n#include <sys/stat.h>\n', '#include "xds_unistd.h"\n', 1),
    # The images are C arrays here (ui/console/images.cpp), not linker blobs.
    ('ui/console/console_ui.cpp', re.compile(r'extern uint8_t (_binary_\w+_start);'), r'extern uint8_t \1[];', 5),
    ('ui/console/console_ui.cpp', '(&_binary_src_ui_console_images_', '(_binary_src_ui_console_images_', 5),
    # Button symbols (Back 0x7F, A, B, X, Start) keep their own colors instead of the text color.
    ('ui/console/console_ui.cpp', 'drawTexture(fontTexture, tx, ty, 47, 47, x1, y, size, size, true, 0, color);',
     'drawTexture(fontTexture, tx, ty, 47, 47, x1, y, size, size, true, 0,\n'
     '            (uint8_t)string[i] >= 0x7F ? 0xFFFFFFFF : color);', 1),
    # xDS360 look: the dark theme becomes black with neon green (0xAABBGGRR):
    # background, text, separators, selected row, selection box, values, headers.
    ('ui/console/console_ui.cpp',
     '0xFF2D2D2D, 0xFFFFFFFF, 0xFF4B4B4B, 0xFF232323, 0xFFE1B955, 0xFFC8FF00, 0xFFA2A2A2, // Dark',
     '0xFF000000, 0xFFE6FFE6, 0xFF1E3A1E, 0xFF0F2A0F, 0xFF14FF39, 0xFF14FF39, 0xFF7FBF7F, // Dark', 1),
    # Xbox button pictures fill their whole cell (main.cpp), so they are wider.
    ('ui/console/console_ui.cpp', '16, 9, 8, 9, 12, 0, 40, 40, 40, 40', '16, 9, 8, 9, 12, 50, 50, 50, 50, 50', 1),
    # A third footer action, bound to Back (Select): on the Xbox, Back exits the
    # file browser and Start goes to the ROMs folder.
    ('ui/console/console_ui.h', 'int &index, std::string actionX = "", std::string actionPlus = "");',
     'int &index, std::string actionX = "", std::string actionPlus = "", std::string actionSelect = "");', 1),
    ('ui/console/console_ui.cpp', 'int &index, std::string actionX, std::string actionPlus) {',
     'int &index, std::string actionX, std::string actionPlus, std::string actionSelect) {', 1),
    ('ui/console/console_ui.cpp', '    if (actionPlus != "") actionPlus = "\\x83 "',
     '    if (actionSelect != "") actionSelect = "\\x7F " + actionSelect + "     ";\n'
     '    if (actionPlus != "") actionPlus = "\\x83 "', 1),
    ('ui/console/console_ui.cpp', '    int boundsPlus = boundsXPlus - stringWidth(actionPlus) * 34 / 48;\n',
     '    int boundsPlus = boundsXPlus - stringWidth(actionPlus) * 34 / 48;\n'
     '    int boundsSelect = boundsPlus - stringWidth(actionSelect) * 34 / 48;\n', 1),
    ('ui/console/console_ui.cpp', 'drawString(actionPlus + actionX + actionB + actionA,',
     'drawString(actionSelect + actionPlus + actionX + actionB + actionA,', 1),
    ('ui/console/console_ui.cpp', '(actionPlus != "" && (pressed & defaultKeys[INPUT_START]))) {',
     '(actionPlus != "" && (pressed & defaultKeys[INPUT_START]))\n'
     '            || (actionSelect != "" && (pressed & defaultKeys[INPUT_SELECT]))) {', 1),
    ('ui/console/console_ui.cpp', '                    return defaultKeys[INPUT_START];\n',
     '                    return defaultKeys[INPUT_START];\n'
     '                else if (touchStart.x >= boundsSelect && touchStart.x < boundsPlus)\n'
     '                    return defaultKeys[INPUT_SELECT];\n', 1),
    ('ui/console/console_ui.cpp', 'menu("NooDS", files, index, "Settings", "Exit");',
     'menu("NooDS", files, index, "Settings", "Roms", "Exit");', 1),
    # The browser and pause menu are titled xDS360 (after the fix above).
    ('ui/console/console_ui.cpp', 'menu("NooDS", ', 'menu("xDS360", ', 2),
    ('ui/console/console_ui.cpp',
     '        else if (pressed & defaultKeys[INPUT_START]) {\n'
     '            // Close the file browser\n',
     '        else if (pressed & defaultKeys[INPUT_START]) {\n'
     '            // Go to the ROMs folder (the one next to noods.ini)\n'
     '            curPath = Settings::basePath + "/roms";\n'
     '            index = 0;\n'
     '        }\n'
     '        else if (pressed & defaultKeys[INPUT_SELECT]) {\n'
     '            // Close the file browser\n', 1),
    # Start in the given folder; B goes up to "" (the drive list on the Xbox).
    ('ui/console/console_ui.cpp', '    basePath = curPath = root;', '    basePath = "";\n    curPath = root;', 1),
    ('ui/console/console_ui.cpp', 'std::to_string(core->fps)', 'std::to_string((long long)core->fps)', 1),
    # "std::vector<T> x = { a, b };" (C++11) -> an array, then the vector built from it.
    ('ui/console/console_ui.cpp', VECTOR_LIST, VECTOR_ARRAY, 15),
    ('ui/screen_layout.cpp', VECTOR_LIST, VECTOR_ARRAY, 1),
]


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def core_sources():
    text = read(SOURCES_MK)
    block = text.split('NOODS_CPP_SOURCES :=', 1)[1].split('\n\n', 1)[0]
    return re.findall(r'[\w/]+\.cpp', block)


def offset(loc):
    return loc.offset


def collect(sources):
    """Return fields with in-class initializers and constructors per class."""
    index = ci.Index.create()
    fields = {}   # (file, start) -> info
    ctors = {}    # (file, body_start) -> info
    records = {}  # usr -> info for structs/classes defined in the core
    for source in sources:
        tu = index.parse(os.path.join(SRC, source), args=CLANG_ARGS)
        errors = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
        if errors:
            for d in errors:
                print('clang:', d, file=sys.stderr)
            sys.exit(1)
        stack = [tu.cursor]
        while stack:
            c = stack.pop()
            f = c.location.file.name if c.location.file else None
            if f and not os.path.abspath(f).startswith(SRC + os.sep):
                continue
            k = c.kind
            if k in (ci.CursorKind.CLASS_DECL, ci.CursorKind.STRUCT_DECL) and c.is_definition():
                toks = list(c.get_tokens())
                brace = next(t for t in toks if t.spelling == '{')
                records[c.get_usr()] = dict(file=os.path.abspath(f), name=c.spelling,
                                            brace=offset(brace.extent.start),
                                            is_class=k == ci.CursorKind.CLASS_DECL)
            elif k == ci.CursorKind.FIELD_DECL:
                kids = list(c.get_children())
                toks = list(c.get_tokens())
                names = [i for i, t in enumerate(toks) if t.extent.start.offset == offset(c.location)]
                if kids and names:
                    # The initializer is the last child expression. It follows
                    # the field name either after '=' or as a bare '{...}', at
                    # the same bracket depth as the name ("void (*f)(T) = x").
                    init = kids[-1]
                    i = names[0]
                    depth = 0
                    for t in toks[:i]:
                        if t.spelling in ('[', '('):
                            depth += 1
                        elif t.spelling in (']', ')'):
                            depth -= 1
                    level = depth
                    cut = None
                    for t in toks[i + 1:]:
                        if t.spelling in ('[', '('):
                            depth += 1
                        elif t.spelling in (']', ')'):
                            depth -= 1
                        elif depth <= level and t.spelling == '=':
                            cut = t
                            break
                        elif depth <= level and t.spelling == '{' and \
                                t.extent.start.offset == init.extent.start.offset:
                            cut = t
                            break
                        elif depth <= level and t.spelling in (',', ';'):
                            break
                    if cut is not None and init.extent.start.offset >= cut.extent.start.offset:
                        key = (os.path.abspath(f), offset(c.location))
                        fields[key] = dict(
                            record=c.semantic_parent.get_usr(), name=c.spelling,
                            cut=offset(cut.extent.start), init=init.extent.start.offset,
                            end=init.extent.end.offset, type=c.type, file=os.path.abspath(f))
            elif k == ci.CursorKind.CONSTRUCTOR and c.is_definition():
                kids = list(c.get_children())
                body = next(x for x in kids if x.kind == ci.CursorKind.COMPOUND_STMT)
                inits = [x.spelling for x in kids if x.kind == ci.CursorKind.MEMBER_REF]
                has_list = any(x.kind in (ci.CursorKind.MEMBER_REF, ci.CursorKind.TYPE_REF)
                               and x.extent.start.offset > offset(c.location) for x in kids
                               if x.kind != ci.CursorKind.PARM_DECL) or ':' in [
                    t.spelling for t in c.get_tokens()
                    if t.extent.start.offset < body.extent.start.offset and t.spelling == ':']
                key = (os.path.abspath(f), body.extent.start.offset)
                ctors[key] = dict(record=c.semantic_parent.get_usr(), inits=set(inits),
                                  body=body.extent.start.offset, has_list=has_list,
                                  file=os.path.abspath(f))
            stack.extend(reversed(list(c.get_children())))
    return fields, ctors, records


def split_top(text):
    parts, depth, cur = [], 0, ''
    for ch in text:
        if ch in '({[':
            depth += 1
        elif ch in ')}]':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return parts


def plan(fields, ctors, records):
    """Build text edits: (file, offset, delete_len, insert_text)."""
    edits = []
    by_record = {}
    for f in fields.values():
        by_record.setdefault(f['record'], []).append(f)
    for usr, flist in by_record.items():
        flist.sort(key=lambda x: x['cut'])
        rec_ctors = [c for c in ctors.values() if c['record'] == usr]
        list_items, body_items = [], []
        for f in flist:
            src = read(f['file'])
            text = src[f['init']:f['end']].strip()
            # Remove " = initializer" (or a bare " {...}") from the declaration.
            start = f['cut']
            while src[start - 1] == ' ':
                start -= 1
            edits.append((f['file'], start, f['end'] - start, ''))
            t = f['type']
            is_array = t.kind == ci.TypeKind.CONSTANTARRAY
            is_record = t.get_canonical().kind == ci.TypeKind.RECORD
            if text.startswith('{'):
                inner = text[1:-1].strip()
                if is_array and inner:
                    values = split_top(inner)
                    list_items.append((f['name'], ''))
                    body_items.append((f['name'], values, t))
                elif is_record and not inner:
                    continue  # default construction is identical
                else:
                    list_items.append((f['name'], ''))  # value-initialize (zero)
            else:
                list_items.append((f['name'], text))
        if not rec_ctors:
            rec = records[usr]
            init = ', '.join('%s(%s)' % (n, v) for n, v in list_items)
            body = ''.join(fill_code(n, vals, t) for n, vals, t in body_items)
            code = '\n    %s()%s {%s}\n' % (rec['name'], ': ' + init if init else '', body)
            if rec['is_class']:  # a class starts private: open and close a public part
                code = '\npublic:' + code + 'private:\n'

            edits.append((rec['file'], rec['brace'] + 1, 0, code))
            continue
        for c in rec_ctors:
            items = [(n, v) for n, v in list_items if n not in c['inits']]
            if items:
                init = ', '.join('%s(%s)' % (n, v) for n, v in items)
                src = read(c['file'])
                pos = c['body']
                while src[pos - 1] in ' \n':
                    pos -= 1
                prefix = ', ' if c['has_list'] else ': '
                edits.append((c['file'], pos, 0, prefix + init))
            body = ''.join(fill_code(n, vals, t) for n, vals, t in body_items
                           if n not in c['inits'])
            if body:
                edits.append((c['file'], c['body'] + 1, 0, body))
    return edits


def fill_code(name, values, t):
    elem = t.element_type.spelling
    return ('\n    { static const %s init_[] = { %s }; memcpy(%s, init_, sizeof(init_)); }\n'
            % (elem, ', '.join(values), name))


def apply_edits(edits):
    by_file = {}
    for e in edits:
        by_file.setdefault(e[0], []).append(e)
    for path, es in by_file.items():
        text = read(path)
        for _, pos, dlen, ins in sorted(es, key=lambda e: e[1], reverse=True):
            text = text[:pos] + ins + text[pos + dlen:]
        if '<cstring>' not in text and 'memcpy(' in ''.join(e[3] for e in es):
            text = text.replace('#pragma once\n', '#pragma once\n\n#include <cstring>\n', 1)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)


def write_images():
    """BMP images of the console UI as C arrays, named like the linker symbols."""
    lines = ['// Generated by tools/xdk/convert_noods.py from NooDS src/ui/console/images (GPL-3.0).',
             '#include <stdint.h>', '']
    for name in UI_IMAGES:
        with open(os.path.join(SRC, 'ui', 'console', 'images', name + '.bmp'), 'rb') as f:
            data = f.read()
        lines.append('uint8_t _binary_src_ui_console_images_%s_bmp_start[] = {' % name)
        for i in range(0, len(data), 32):
            lines.append(','.join(str(b) for b in data[i:i + 32]) + ',')
        lines.append('};')
        lines.append('')
    with open(os.path.join(OUT, 'ui', 'console', 'images.cpp'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))


def main():
    if not os.path.isdir(SRC):
        sys.exit('Run scripts/fetch_noods.sh first: %s is missing' % SRC)
    sources = ['core/' + s for s in core_sources()] + UI_SOURCES
    # Work on a fresh copy so offsets match what clang parses.
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(os.path.join(SRC, 'core'), os.path.join(OUT, 'core'),
                    ignore=shutil.ignore_patterns('*.o', '*.d'))
    for rel in UI_FILES:
        os.makedirs(os.path.dirname(os.path.join(OUT, rel)), exist_ok=True)
        shutil.copy(os.path.join(SRC, rel), os.path.join(OUT, rel))
    fields, ctors, records = collect(sources)
    # Rebase the analysis from SRC onto the copy.
    def rebase(d):
        d['file'] = d['file'].replace(SRC, OUT, 1)
    for d in list(fields.values()) + list(ctors.values()) + list(records.values()):
        rebase(d)
    edits = plan(fields, ctors, records)
    apply_edits(edits)
    for rel, old, new, count in TEXT_FIXES:
        path = os.path.join(OUT, rel)
        text = read(path)
        if isinstance(old, str):
            found = text.count(old)
            text = text.replace(old, new)
        else:
            text, found = old.subn(new, text)
        if found != count:
            sys.exit('%s: expected %d match(es) of %r, found %d' % (rel, count, str(old)[:60], found))
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
    # Keep only what the Xbox build compiles, plus every header.
    keep = set(sources)
    for dirpath, _, files in os.walk(OUT):
        for name in files:
            rel = os.path.relpath(os.path.join(dirpath, name), OUT).replace(os.sep, '/')
            if name.endswith('.cpp') and rel not in keep:
                os.remove(os.path.join(dirpath, name))
    write_images()
    shutil.copy(os.path.join(REPO, 'third_party', 'noods', 'LICENSE'), os.path.join(OUT, 'LICENSE'))
    print('fields moved: %d, constructors touched: %d, files: %s' % (
        len(fields), len({e[0] for e in edits}), OUT))


if __name__ == '__main__':
    main()
