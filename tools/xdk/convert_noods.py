#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert the pinned, Xbox-patched NooDS core into C++ that VS2010 accepts.

Input:  third_party/noods/src/core (after scripts/fetch_noods.sh)
Output: xdk/noods (committed, so the Windows build needs no scripts)

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
SRC = os.path.join(REPO, 'third_party', 'noods', 'src', 'core')
OUT = os.path.join(REPO, 'xdk', 'noods')
SOURCES_MK = os.path.join(REPO, 'platform', 'xenon', 'noods_sources.mk')
RESOURCE_DIR = os.popen('clang -print-resource-dir').read().strip()
CLANG_ARGS = ['-resource-dir', RESOURCE_DIR, '-x', 'c++', '-std=c++11', '-DNOODS_XENON=1', '-DLOG_LEVEL=0',
              '-DENDIAN_BIG=1', '-DNO_FDOPEN=1', '-I' + SRC]

# Text fixes that the member-initializer pass cannot express. Each pattern must
# match exactly the given number of times.
TEXT_FIXES = [
    # GCC builtins and attributes -> portable helpers from xds_compat.h.
    ('defines.h', '#include <cstdio>\n', '#include <cstdio>\n#include "xds_compat.h"\n', 1),
    ('defines.h', '__builtin_memcpy', 'memcpy', 4),
    ('defines.h', '__builtin_bswap16', 'XDS_BSWAP16', 3),
    ('defines.h', '__builtin_bswap32', 'XDS_BSWAP32', 4),
    ('defines.h', '__builtin_bswap64', 'XDS_BSWAP64', 1),
    ('defines.h', 'typedef uint16_t AliasUint16 __attribute__((may_alias));',
     'typedef uint16_t AliasUint16;', 1),
    ('defines.h', 'typedef uint32_t AliasUint32 __attribute__((may_alias));',
     'typedef uint32_t AliasUint32;', 1),
    ('defines.h', '#include <unistd.h>', '#include "xds_unistd.h"', 1),
    # VS2010 only has std::to_string(long long / unsigned long long / long double).
    ('settings.cpp', 'std::to_string(*(int*)settings[i].value)',
     'std::to_string((long long)*(int*)settings[i].value)', 1),
    ('memory/cartridge.cpp', 'std::to_string(core->id + 1)',
     'std::to_string((long long)(core->id + 1))', 2),
    ('settings.cpp', '#include <sys/stat.h>', '#include "xds_unistd.h"', 1),
    # Assigning an empty braced list to a container is C++11-only.
    ('io/aes.cpp', 'writeFifo = {};', 'writeFifo.clear();', 1),
    ('io/aes.cpp', 'readFifo = {};', 'readFifo.clear();', 1),
    ('memory/sd_mmc.cpp', 'dataFifo32 = {};', 'dataFifo32.clear();', 1),
    # VS2010's math.h has float/double/long double overloads, so sqrt(integer)
    # is ambiguous. GCC converts the integer to double; do the same explicitly.
    ('hle/hle_bios.cpp', '*registers[0] = sqrt(*registers[0]);',
     '*registers[0] = sqrt((double)*registers[0]);', 1),
    ('io/div_sqrt.cpp', 'sqrtResult = sqrt((uint32_t)sqrtParam);',
     'sqrtResult = sqrt((double)(uint32_t)sqrtParam);', 1),
    # Save/state/cheat folders: the Xbox needs "\\" in paths (game:\\saves\\x.sav).
    ('memory/cartridge.cpp', 'Settings::basePath + "/saves"', 'Settings::basePath + XDS_SLASH "saves"', 1),
    ('memory/cartridge.cpp', 'Settings::basePath + "/states"', 'Settings::basePath + XDS_SLASH "states"', 1),
    ('memory/cartridge.cpp', 'Settings::basePath + "/cheats"', 'Settings::basePath + XDS_SLASH "cheats"', 1),
    # A static std::vector filled from a braced list -> fill it from an array.
    ('settings.cpp', re.compile(r'std::vector<Setting> Settings::settings = \{(.*?)\n\};\n', re.S),
     lambda m: ('static Setting xdsSettingsInit[] = {%s\n};\nstd::vector<Setting> Settings::settings(xdsSettingsInit,\n'
                '    xdsSettingsInit + sizeof(xdsSettingsInit) / sizeof(xdsSettingsInit[0]));\n'
                % re.sub(r'&(\w+),', r'&Settings::\1,', m.group(1))), 1),
    # Arrays of objects without a default constructor -> XdsArray (built in place).
    ('core.h', '    Dma dma[2];', '    XdsArray<Dma, 2> dma;', 1),
    ('core.h', '    Gpu2D gpu2D[2];', '    XdsArray<Gpu2D, 2> gpu2D;', 1),
    ('core.h', '    HleBios hleBios[3];', '    XdsArray<HleBios, 3> hleBios;', 1),
    ('core.h', '    Interpreter interpreter[2];', '    XdsArray<Interpreter, 2> interpreter;', 1),
    ('core.h', '    Ndma ndma[2];', '    XdsArray<Ndma, 2> ndma;', 1),
    ('core.h', '    Timers timers[2];', '    XdsArray<Timers, 2> timers;', 1),
    ('core.cpp', re.compile(r'dma \{ Dma\(this, 0\), Dma\(this, 1\) \}'), 'dma(this)', 1),
    ('core.cpp', re.compile(r'gpu2D \{ Gpu2D\(this, 0\), Gpu2D\(this, 1\) \}'), 'gpu2D(this)', 1),
    ('core.cpp', re.compile(r'interpreter \{ Interpreter\(\s*this, 0\), Interpreter\(this, 1\) \}'),
     'interpreter(this)', 1),
    ('core.cpp', re.compile(r'ndma \{ Ndma\(this, 0\), Ndma\(this, 1\) \}'), 'ndma(this)', 1),
    ('core.cpp', re.compile(r'timers \{ Timers\(this, 0\), Timers\(this, 1\) \}'), 'timers(this)', 1),
    ('core.cpp', re.compile(r'hleBios \{ (HleBios\(this, 0, HleBios::swiTable9\)), HleBios\(this, 1, HleBios::\s*'
                            r'swiTable7\), (HleBios\(this, 1, HleBios::swiTableGba\)) \}'),
     r'hleBios(\1, HleBios(this, 1, HleBios::swiTable7), \2)', 1),
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
            if f and not os.path.abspath(f).startswith(SRC):
                continue
            k = c.kind
            if k in (ci.CursorKind.CLASS_DECL, ci.CursorKind.STRUCT_DECL) and c.is_definition():
                toks = list(c.get_tokens())
                brace = next(t for t in toks if t.spelling == '{')
                records[c.get_usr()] = dict(file=os.path.abspath(f), name=c.spelling,
                                            brace=offset(brace.extent.start))
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


def main():
    if not os.path.isdir(SRC):
        sys.exit('Run scripts/fetch_noods.sh first: %s is missing' % SRC)
    sources = core_sources()
    # Work on a fresh copy so offsets match what clang parses.
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(SRC, OUT, ignore=shutil.ignore_patterns('*.o', '*.d'))
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
    shutil.copy(os.path.join(REPO, 'third_party', 'noods', 'LICENSE'), os.path.join(OUT, 'LICENSE'))
    print('fields moved: %d, constructors touched: %d, files: %s' % (
        len(fields), len({e[0] for e in edits}), OUT))


if __name__ == '__main__':
    main()
