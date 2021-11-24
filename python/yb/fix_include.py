import argparse
import enum
import json
import multiprocessing
import os
import random
import re
import shlex
import string
import subprocess
import sys
import tempfile
import time
import typing

PREFIXES = ['../../ent/src/', '../../src/', 'src/']
INCLUDE_RE = re.compile(R'^#include\s+"([^"]+)"$')
SYSTEM_INCLUDE_RE = re.compile(R'^#include\s+<([^>]+)>$')
IGNORE_RE = re.compile(R'^(?://.*|using\s.*)$')
IF_OPEN_RE = re.compile(R'^#if.*$')
IF_CLOSE_RE = re.compile(R'^#endif.*$')

headers = {}
nodes = {}
missing = set()
node_stack = []
compdb = {}
max_jobs = multiprocessing.cpu_count()
temp_dir = '/tmp'
next_task_id = 0
filter_re = None


def replace_ext(fname: str, new_ext: str) -> str:
    return os.path.splitext(fname)[0] + new_ext


def random_fname():
    return ''.join(random.choices(string.ascii_letters + string.digits, k=8))


def pick_task_id():
    global next_task_id
    next_task_id += 1
    return next_task_id


temp_name = random_fname()


def fname_for_task(task_idx, ext):
    return '{}.{}.{}'.format(temp_name, task_idx, ext)


def error(message, *kwargs):
    sys.stderr.write((message + '\n').format(*kwargs))
    sys.exit(1)


class NodeState(enum.Enum):
    IDLE = 0
    PROCESSING = 1
    PROCESSED = 2


class Include(typing.NamedTuple):
    name: str
    line_no: int
    system: bool
    if_nesting: int


class ParsedFile:
    def __init__(self):
        self.includes: typing.List[Include] = []
        self.trivial = True


def parse(fname, lines) -> ParsedFile:
    header = fname.endswith('.h')
    line_no = -1
    result = ParsedFile()
    last_unknown = None
    if_nesting = 0
    prev_lines = ['', '']
    for line in lines:
        line_no += 1
        line = line.rstrip()
        for i in range(len(prev_lines) - 1):
            prev_lines[i + 1] = prev_lines[i]
        prev_lines[0] = line
        if len(line) == 0 or IGNORE_RE.match(line):
            continue
        if IF_OPEN_RE.match(line):
            if_nesting += 1
            continue
        elif IF_CLOSE_RE.match(line):
            if_nesting -= 1
            continue
        if header and if_nesting == 1 and line.startswith("#define") and\
                prev_lines[1] == "#ifndef" + line[7:]:
            if_nesting -= 1
            continue
        match = INCLUDE_RE.match(line)
        system = False
        if not match:
            match = SYSTEM_INCLUDE_RE.match(line)
            system = True
        if match:
            name = match[1]
            if result.trivial and last_unknown is not None:
                result.trivial = False
                if not header:
                    print("{} non trivial because of {}".format(fname, last_unknown))
            if not system and name not in nodes:
                if name not in missing and result.trivial:
                    print("File not found: {} in {}".format(name, fname))
                    missing.add(name)
                continue
            if result.trivial and if_nesting != 0:
                result.trivial = False
            result.includes.append(Include(name, line_no, system, if_nesting))
        else:
            last_unknown = line
    return result


class Node:
    def __init__(self, name, fname):
        self.name = name
        self.fname = fname
        self.refs: typing.Set[str] = set()
        self.state = NodeState.IDLE
        self.parsed: typing.Union[ParsedFile, None] = None

    def process(self):
        if self.state == NodeState.PROCESSED:
            return
        node_stack.append(self.name)
        try:
            if self.state != NodeState.IDLE:
                print("Cycle found: {}".format(node_stack))
                return
            self.state = NodeState.PROCESSING
            with open(self.fname) as inp:
                self.parsed = parse(self.fname, inp)
            for include in self.parsed.includes:
                self.refs.add(include.name)
                if not include.system:
                    nodes[include.name].process()
                    self.refs.update(nodes[include.name].refs)
            self.state = NodeState.PROCESSED
        finally:
            popped = node_stack.pop()
            if popped != self.name:
                error('Stack failure {} vs {}', popped, self.name)

    def __repr__(self):
        return "{{ name: {} state: {} parsed: {} }}".format(self.name, self.state, self.parsed)


class CheckableInclude(typing.NamedTuple):
    name: str
    line_no: int
    task_id: int


class Step(enum.Enum):
    REMOVE = 0
    INLINE = 1
    FINISH = 2


def extract_includes(includes: typing.Dict[str, bool], filter) -> typing.List[str]:
    result = []
    todel = []
    for include, system in includes.items():
        if filter(include, system):
            todel.append(include)
            result.append("#include " + ('<{}>' if system else '"{}"').format(include))
    for include in todel:
        del includes[include]
    result.sort()
    if len(result) > 0:
        result.append("")
    return result


lib_headers = frozenset([
    'ev++.h',
    'squeasel.h',
    ])

lib_header_prefixes = frozenset([
    'boost',
    'cds',
    'cpp_redis',
    'google',
    'gflags',
    'glog',
    'gmock',
    'gtest',
    'rapidjson',
    'tacopie',
    ])


def include_prefix(include: str) -> str:
    idx = include.find('/')
    return "" if idx == -1 else include[:idx]


def is_c_system_header(include: str, system: bool) -> bool:
    return system and include.find(".") != -1 and (include not in lib_headers) and \
           (include_prefix(include) not in lib_header_prefixes)


def is_cpp_system_header(include: str, system: bool) -> bool:
    return system and include.find(".") == -1


class GeneratedSource:
    def __init__(self, fname):
        self.fname = fname
        self.header_lines = []
        self.footer_lines = []
        self.includes = {}

    def append(self, line: str):
        m = INCLUDE_RE.match(line)
        system = False
        if not m:
            m = SYSTEM_INCLUDE_RE.match(line)
            system = True
        if m:
            self.header_lines += self.footer_lines
            self.footer_lines.clear()
            include = m[1]
            if include in self.includes:
                return
            self.includes[include] = system
            return
        self.footer_lines.append(line)

    def extend(self, other):
        for line in other:
            self.append(line)

    def complete(self) -> typing.List[str]:
        i = len(self.header_lines)
        while i > 0 and self.header_lines[i - 1] == "":
            i -= 1
        result = self.header_lines[:i]
        includes = dict(self.includes)
        result += extract_includes(
            includes, lambda include, system: self.fname.endswith(replace_ext(include, '.cc')))
        result += extract_includes(includes, is_c_system_header)
        result += extract_includes(includes, is_cpp_system_header)
        result += extract_includes(includes, lambda include, system: system)
        result += extract_includes(includes, lambda include, system: True)
        i = 0
        while i < len(self.footer_lines) and self.footer_lines[i] == "":
            i += 1
        result += self.footer_lines[i:]
        return result


class SourceFile:
    def __init__(self, fname):
        self.fname = fname
        self.lines = None
        self.patched = False
        self.modified_lines: typing.Dict[int, str] = {}
        self.checked: typing.Set[typing.Tuple[string, Step]] = set()
        self.unchecked: typing.List[CheckableInclude] = []
        self.running: typing.Union[CheckableInclude, None] = None
        self.includes: typing.Union[typing.List[Include], None] = None
        self.step = Step.REMOVE

    def prepare(self):
        if self.lines is None:
            with open(self.fname) as inp:
                self.lines = inp.read().split('\n')
            parsed = parse(self.fname, self.lines)
            if parsed.trivial:
                self.includes = parsed.includes
        if self.includes is None:
            return
        for include in self.includes:
            cc_file = replace_ext(include.name, '.cc')
            if self.fname.endswith(cc_file):
                continue
            if (include.name, self.step) in self.checked or include.line_no in self.modified_lines:
                continue
            unique = True
            for other in self.includes:
                if other.line_no == include.line_no or other.name not in nodes:
                    continue
                refs = nodes[other.name].refs
                if include.name in refs:
                    unique = False
                    break
            if not unique:
                continue
            if self.step == Step.INLINE:
                if include.system or include.name not in nodes or \
                        not nodes[include.name].parsed.trivial:
                    continue
            self.unchecked.append(CheckableInclude(include.name, include.line_no, pick_task_id()))

    def perform(self):
        if len(self.unchecked) != 0:
            return self.execute()
        while True:
            self.prepare()
            if len(self.unchecked) != 0:
                return self.execute()
            self.step = Step(self.step.value + 1)
            if self.step == Step.FINISH:
                return None

    def execute(self):
        command = []
        replace = None
        self.running = self.unchecked.pop()
        if isinstance(compdb[self.fname], str):
            compdb[self.fname] = shlex.split(compdb[self.fname])
        for arg in compdb[self.fname]:
            if replace is not None:
                command.append(replace)
                replace = None
                continue
            if arg == '-MT' or arg == '-o':
                replace = os.path.join(temp_dir, fname_for_task(self.running.task_id, 'o'))
            elif arg == '-MF':
                replace = os.path.join(temp_dir, fname_for_task(self.running.task_id, 'o.d'))
            elif arg == '-c':
                replace = self.temp_source(self.running.task_id)
            command.append(arg)
        print("Checking {}: {} {}".format(self.fname, self.step, self.running[0]))
        inline = None if self.step == Step.REMOVE else self.running.name
        self.write_source(self.temp_source(self.running.task_id), (self.running.line_no, inline))
        return subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def temp_source(self, task_idx):
        return replace_ext(self.fname, '.' + fname_for_task(task_idx, 'cc'))

    def generate_lines(self, modified_line):
        modified_lines: typing.List[typing.Tuple[int, str]] = list(self.modified_lines.items())
        if modified_line is not None:
            modified_lines.append(modified_line)
        modified_lines.sort()
        result = GeneratedSource(self.fname)
        p = 0
        for modification in modified_lines:
            if p != modification[0]:
                result.extend(self.lines[p:modification[0]])
            if modification[1] is not None:
                node: Node = nodes[modification[1]]
                for include in node.parsed.includes:
                    if include.system:
                        result.append('#include <{}>'.format(include.name))
                    else:
                        result.append('#include "{}"'.format(include.name))
            p = modification[0] + 1
        result.extend(self.lines[p:])
        return result.complete()

    def write_source(self, fname, modification):
        new_lines = self.generate_lines(modification)
        with open(fname, 'w') as out:
            out.write('\n'.join(new_lines))

    def complete(self):
        if len(self.modified_lines) == 0:
            if self.patched:
                self.write_source(self.fname, None)
            return None
        self.patched = True
        self.lines = self.generate_lines(None)
        parsed = parse(self.fname, self.lines)
        self.includes = parsed.includes if parsed.trivial else None
        self.step = Step.REMOVE
        self.modified_lines.clear()
        return self.next_task()

    def next_task(self):
        result = self.perform()
        if result is None:
            return self.complete()
        return result

    def handle_result(self, return_code):
        os.unlink(self.temp_source(self.running.task_id))
        self.checked.add((self.running.name, self.step))
        if return_code == 0:
            print("Could {} {} from {}".format(self.step, self.running.name, self.fname))
            inline = None if self.step == Step.REMOVE else self.running.name
            self.modified_lines[self.running.line_no] = inline
        return self.next_task()


def prefix_idx(fname):
    for i in range(len(PREFIXES)):
        if fname.startswith(PREFIXES[i]):
            return i
    return None


def parse_comp_db():
    json_str = subprocess.check_output(['ninja', '-t', 'compdb'])
    compdb_json = json.loads(json_str)
    for entry in compdb_json:
        compdb[entry['file']] = entry['command']


class RunningTask(typing.NamedTuple):
    file: SourceFile
    process: subprocess.Popen


def cleanup(sources: typing.List[SourceFile]):
    pending_source = 0
    running_tasks: typing.List[RunningTask] = []
    while len(running_tasks) != 0 or len(sources) > pending_source:
        need_wait = True
        while len(running_tasks) < max_jobs and len(sources) > pending_source:
            source = sources[pending_source]
            process = source.perform()
            pending_source += 1
            if process is None:
                continue
            running_tasks.append(RunningTask(source, process))
        i = 0
        while i < len(running_tasks):
            return_code = running_tasks[i].process.poll()
            if return_code is None:
                i += 1
                continue
            need_wait = False
            task = running_tasks[i]
            new_process = task.file.handle_result(return_code)
            if new_process is None:
                running_tasks[i] = running_tasks[len(running_tasks) - 1]
                running_tasks.pop()
            else:
                running_tasks[i] = RunningTask(task.file, new_process)
        if need_wait:
            time.sleep(0.1)


def parse_deps(filter):
    global filter_re
    filter_re = re.compile(filter) if filter is not None else None
    sources = []
    with open('deps.txt') as inp:
        first = False
        for line in inp:
            line = line.rstrip()
            if not line.startswith(' '):
                first = True
                continue
            fname = line.lstrip()
            if first:
                if fname.startswith('../../src/'):
                    if True if filter_re is None else filter_re.match(fname):
                        sources.append(SourceFile(fname))
                first = False
            elif fname.startswith('/') or fname.startswith('postgres'):
                continue
            elif fname.endswith(".h"):
                idx = prefix_idx(fname)
                if idx is None:
                    error("Not found prefix for {}", fname)
                ref_name = fname[len(PREFIXES[idx]):]
                if ref_name in headers:
                    if headers[ref_name] != fname and idx < prefix_idx(headers[ref_name]):
                        headers[ref_name] = fname
                        print("Update: {} => {}", headers[ref_name], fname)
                else:
                    headers[ref_name] = fname
    return sources


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--filter')
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    print("Parsing compdb")
    parse_comp_db()
    print("Parsing deps")
    sources = parse_deps(args.filter)

    for name, fname in headers.items():
        nodes[name] = Node(name, fname)

    for node in nodes.values():
        node.process()

    with tempfile.TemporaryDirectory() as tmpdirname:
        global temp_dir
        temp_dir = tmpdirname
        print("Try cleanup {} sources".format(len(sources)))
        cleanup(sources)


if __name__ == '__main__':
    main()
