#!/usr/bin/env python3
# Copyright (c) YugabyteDB, Inc.
"""Writes hba_pg_rules.jsonl: HBA lines and how YB's Postgres parses each one.

Each output row is [line, "valid" | "invalid" | "none", ldapbindpasswd as Postgres read it].
The lines are written into the ysql_hba.conf of a running local cluster (back it up first) and
read back through pg_hba_file_rules, which parses the file without reloading it.

Usage: gen_hba_pg_rules.py YSQLSH HBA_CONF_PATH OUT [--big]
--big writes ~200k lines for a one-off local check instead of the committed set.
"""
import itertools
import json
import random
import subprocess
import sys

QUERY = ("select json_build_object('n', line_number, 'e', error, 'o', options)::text "
         "from pg_hba_file_rules")
PREFIXES = [
    'local all all ldap ldapserver=h ldapbasedn=x ',
    'host all all 0.0.0.0/0 ldap ldapserver=h ldapbasedn=x ',
    '\tlocal all all ldap ldapbasedn=x ldapserver=h,',
]
SMALL = ['a', ' ', '"', '#', ',', '=', '\t', '\\']
SUFFIXES = ['', ' ldaptls=1', ' junk', ',ldaptls=1', ' key=abc']
WIDE = SMALL + list('bcXYZ019!@$%^&*()[]{}<>?/|~;:\'`-_+.')


def pg_parse(ysqlsh, hba, lines):
    with open(hba, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    out = subprocess.run(
        [ysqlsh, '-h', '127.0.0.1', '-U', 'yugabyte', '-d', 'yugabyte', '-At', '-c', QUERY],
        capture_output=True, text=True, check=True).stdout
    rows = {}
    for row in out.splitlines():
        r = json.loads(row)
        rows[r['n']] = r
    results = []
    for i, line in enumerate(lines, 1):
        r = rows.get(i)
        if r is None:
            results.append([line, 'none', None])
        elif r['e']:
            results.append([line, 'invalid', None])
        else:
            passwd = None
            for o in r['o'] or []:
                if o.startswith('ldapbindpasswd='):
                    passwd = o[len('ldapbindpasswd='):]
            results.append([line, 'valid', passwd])
    return results


def ok(line):
    # A trailing backslash would join the next line; CR/LF can't appear in one line.
    return '\n' not in line and '\r' not in line and not line.endswith('\\')


def exhaustive(max_len, suffixes, prefix):
    for n in range(max_len + 1):
        for chars in itertools.product(SMALL, repeat=n):
            t = ''.join(chars)
            for s in suffixes:
                yield prefix + 'ldapbindpasswd=' + t + s


def variants():
    tokens = ['a', 'a b', '"a b"', 'a"b', '"a""b"', '', '""', 'a#b', '"a#b"', 'a,b', '"a,b"']
    for t in tokens:
        for prefix in PREFIXES:
            # Quoted option names (ldap"bind"passwd=) are tested in HbaConfRedactorTest.
            yield prefix + '"ldapbindpasswd=' + t + '"'
            yield prefix + 'LDAPBINDPASSWD=' + t
            yield prefix + 'ldapbindpasswd=' + t + ' ldapbindpasswd=second'
            yield prefix + 'ldapbindpasswd=first ldapbindpasswd=' + t
            yield prefix + 'ldapbindpasswd= ' + t
        yield ' "local all all ldap ldapbasedn=x ldapbindpasswd=' + t + '"'
        yield '# local all all ldap ldapbasedn=x ldapbindpasswd=' + t


def random_lines(rng, count):
    for _ in range(count):
        prefix = rng.choice(PREFIXES)
        body = ''.join(rng.choice(WIDE) for _ in range(rng.randint(4, 12)))
        token = '"' + body.replace('"', '""') + '"' if rng.random() < 0.4 else body
        yield prefix + 'ldapbindpasswd=' + token + rng.choice(SUFFIXES)


def main():
    ysqlsh, hba, out_path = sys.argv[1:4]
    big = '--big' in sys.argv[4:]
    rng = random.Random(23624)
    if big:
        lines = []
        for prefix in PREFIXES:
            lines += list(exhaustive(4, SUFFIXES, prefix))
        lines += list(exhaustive(5, ['', ' ldaptls=1', ' junk'], PREFIXES[0]))
        lines += list(variants())
        lines += list(random_lines(rng, 50000))
    else:
        lines = list(exhaustive(3, [''], PREFIXES[0]))
        lines += list(exhaustive(2, [' junk'], PREFIXES[0]))
        lines += list(variants())
        lines += list(random_lines(rng, 200))
    lines = [line for line in dict.fromkeys(lines) if ok(line)]
    results = []
    for i in range(0, len(lines), 20000):
        results += pg_parse(ysqlsh, hba, lines[i:i + 20000])
    with open(out_path, 'w') as f:
        for row in results:
            f.write(json.dumps(row, ensure_ascii=False, separators=(',', ':')) + '\n')
    print(f'{len(results)} lines')


if __name__ == '__main__':
    main()
