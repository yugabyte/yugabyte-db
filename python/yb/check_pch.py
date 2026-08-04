import os
import re
import subprocess


TARGET_RE = re.compile(R'^\s*(.*): phony\s*$')
SECTION_RE = re.compile(R'^([^\s]+):.*$')
INPUT_RE = re.compile(R'^\s\sinput:.*$')
OUTPUT_RE = re.compile(R'^\s\soutputs:$')
SOURCE_FILE_NAME_RE = re.compile(R'^\.\./\.\./src/yb/([^/]+)/.*\.cc$')
TEST_OBJ_RE = re.compile(R'^.*/[^/]*test[^/]*\.cc\.o$')


def exec(*args):
    return subprocess.check_output(args).decode('utf-8').split('\n')


def ninja_tool(*args):
    return exec('ninja', '-t', *args)


def query_inputs(targets):
    inputs = {}
    target = None
    input_section = False

    for line in ninja_tool('query', *targets):
        m = SECTION_RE.match(line)
        if m:
            input_section = False
            target = m[1]
            continue
        if INPUT_RE.match(line):
            input_section = True
            continue
        if OUTPUT_RE.match(line):
            input_section = False
            continue
        if input_section:
            if target not in inputs:
                inputs[target] = []
            inputs[target].append(line.strip())

    return inputs


def main():
    obj_file = None
    has_pch = False
    pch2dir = {}
    no_pch_list = []
    for line in ninja_tool('deps'):
        m = SECTION_RE.match(line)
        if m:
            obj_file = m[1]
            if not obj_file.endswith('.cc.o'):
                obj_file = None
            has_pch = False
            continue
        if obj_file is None:
            continue
        if not line.startswith('    '):
            if not has_pch:
                no_pch_list.append(obj_file)
            obj_file = None
            continue
        dep_name = line.strip()
        if dep_name.endswith('_pch.h'):
            has_pch = True
            dir = os.path.dirname(dep_name)
            base = os.path.basename(dep_name)
            if base in pch2dir:
                if pch2dir[base] != dir:
                    print("{} has multiple locations: {} and {}".format(base, dir, pch2dir))
            else:
                pch2dir[base] = dir
    for file in sorted(no_pch_list):
        print("{} does not use pch".format(file))


if __name__ == '__main__':
    main()
