#!/usr/bin/env python3
"""Compara o layout de ARDOUR::Session que NOSSOS headers produzem com o layout do
libardour INSTALADO (via DWARF do debuginfod do Arch).

Uso (chamado por tools/abi-check.sh):
    abi.py parse-lib   <ptype.txt>        -> lib_members.json
    abi.py gen-probe   <lib_members.json> <session_pub.h> <offdump.cc>
    abi.py compare     <lib_members.json> <our_offsets.txt>
"""
import json
import re
import sys

# membro: "/*  12480      |      24 */    ARDOUR::RouteGroupList _route_groups;"
MEM = re.compile(r'^/\*\s*(\d+)\s*\|\s*(\d+)\s*\*/\s{4}(\S.*)$')
TOTAL = re.compile(r'total size \(bytes\):\s*(\d+)')


def parse_lib(path):
    """Extrai (offset, size, nome) dos membros TOP-LEVEL da classe Session."""
    out = []
    total = None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = TOTAL.search(line)
        if m:
            total = int(m.group(1))
        m = MEM.match(line)
        if not m:
            continue
        off, size, decl = int(m.group(1)), int(m.group(2)), m.group(3).strip()
        # bases (class/struct/union/enum) aparecem inline no dump; pulamos
        if decl.startswith(('class ', 'struct ', 'union ', 'enum ')):
            continue
        name = decl.rstrip(';').strip()
        if ':' in name or name.endswith(('{', '}')):
            continue  # bitfield ou linha de fechamento de bloco aninhado
        m2 = re.match(r'^.*?([A-Za-z_][A-Za-z0-9_]*)(\[\d+\])?$', name)
        if not m2:
            continue
        out.append([off, size, m2.group(1)])
    return out, total


def gen_probe(lib_json, session_pub, out_cc):
    mem = json.load(open(lib_json))
    names, seen = [], set()
    for off, size, n in mem:
        if n not in seen:
            seen.add(n)
            names.append(n)
    src = [
        '#include "%s"' % session_pub,
        '#include <cstddef>',
        '#include <cstdio>',
        'int main () {',
        '  std::printf ("SIZEOF %llu\\n", (unsigned long long) sizeof (ARDOUR::Session));',
    ]
    for n in names:
        src.append('  std::printf ("%s %%llu\\n", (unsigned long long) offsetof (ARDOUR::Session, %s));' % (n, n))
    src.append('  return 0;')
    src.append('}')
    open(out_cc, 'w').write('\n'.join(src) + '\n')
    return len(names)


def compare(lib_json, ours_txt):
    lib = {}
    for off, size, n in json.load(open(lib_json)):
        lib.setdefault(n, (off, size))
    ours, size_ours = {}, None
    for line in open(ours_txt):
        p = line.split()
        if len(p) == 2:
            if p[0] == 'SIZEOF':
                size_ours = int(p[1])
            else:
                ours[p[0]] = int(p[1])
    bad = []
    for n, (off, size) in lib.items():
        if n in ours and ours[n] != off:
            bad.append((n, off, ours[n]))
    return len(ours), size_ours, bad


def main():
    cmd = sys.argv[1]
    if cmd == 'parse-lib':
        mem, total = parse_lib(sys.argv[2])
        json.dump(mem, open('lib_members.json', 'w'))
        print('membros top-level: %d   sizeof(Session) na lib: %s' % (len(mem), total))
        print('SIZEOF_LIB=%s' % total)
    elif cmd == 'gen-probe':
        print('offsets no probe: %d' % gen_probe(sys.argv[2], sys.argv[3], sys.argv[4]))
    elif cmd == 'compare':
        n, size, bad = compare(sys.argv[2], sys.argv[3])
        print('nosso sizeof(ARDOUR::Session) = %s' % size)
        print('membros conferidos: %d' % n)
        if bad:
            print('\n*** DIVERGENCIA DE ABI ***')
            for name, o, g in bad[:20]:
                print('  %-40s lib=%-6d nosso=%-6d delta=%+d' % (name, o, g, g - o))
            print('total divergente: %d' % len(bad))
            sys.exit(1)
        print('OK — layout identico ao da lib instalada.')
    else:
        sys.exit('comando desconhecido: ' + cmd)


if __name__ == '__main__':
    main()
