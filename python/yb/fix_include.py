import argparse
import cpp_parser
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

EXTERNAL_PREFIXES = ['/usr/include/c++/v1/', '/usr/include/', '/include/rapidjson/', '/include/',
                     '/include-fixed/']

build_dir = os.getcwd()
root_dir = os.path.dirname(os.path.dirname(build_dir))
prefixes = [os.path.join(root_dir, 'src/'), os.path.join(build_dir, 'src/')]

nodes: typing.Dict[str, 'Node'] = {}
missing = set()
node_stack = []
compdb = {}
max_jobs = multiprocessing.cpu_count()
temp_dir = '/tmp'
header_cc = 'header.cc'
next_task_id = 0
filter_re = None
debug_filter_re = None
sources = []
added_includes = {}


def replace_ext(fname: str, new_ext: str) -> str:
    return os.path.splitext(fname)[0] + new_ext


def is_header(fname: str) -> bool:
    return fname.endswith('.h')


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


class Node:
    def __init__(self, name, fname, external, prefix_idx):
        self.name = name
        self.fname = fname
        self.external = external
        self.prefix_idx = prefix_idx
        self.refs: typing.Set[str] = set()
        self.state = NodeState.IDLE
        self.parsed: typing.Union[cpp_parser.ParsedFile, None] = None
        self.included_by: typing.Set[Node] = set()
        self.includes = set()
        self.not_cleaned_includes = 0
        self.removed_includes: typing.List[str] = []

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
                self.parsed = cpp_parser.parse(self.fname, inp)
            for include in self.parsed.includes:
                self.refs.add(include.name)
                if include.name in nodes:
                    included_node = nodes[include.name]
                    included_node.included_by.add(self)
                    self.includes.add(included_node)
                    included_node.process()
                    self.refs.update(included_node.refs)
            self.state = NodeState.PROCESSED
            if not self.external and self.parsed.trivial and source_file(self.fname):
                sources.append(SourceFile(self.fname, self))
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


def extract_include_key(include_line: str) -> str:
    if not include_line.endswith('_fwd.h"'):
        return include_line
    pos = include_line.rfind('/')
    if pos == -1:
        return include_line
    return include_line[:pos]


def extract_includes(includes: typing.Dict[str, typing.Tuple[bool, str]], filter) \
        -> typing.List[str]:
    result = []
    todel = []
    for include, (system, tail) in includes.items():
        if filter(include, system):
            todel.append(include)
            result.append(cpp_parser.gen_line(include, system) + tail)
    for include in todel:
        del includes[include]
    result.sort(key=extract_include_key)
    if len(result) > 0:
        result.append("")
    return result


class GeneratedSource:
    def __init__(self, fname):
        self.fname = fname
        self.header_lines = []
        self.footer_lines = []
        self.includes = {}

    def append(self, line: str):
        parsed_include = cpp_parser.parse_include_line(line)
        if parsed_include is not None:
            if len(self.includes) == 0:
                self.header_lines += self.footer_lines
                self.footer_lines.clear()
            if parsed_include.file in self.includes:
                return
            self.includes[parsed_include.file] = (parsed_include.system, parsed_include.tail)
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
        if i > 0:
            result.append('')
        includes = dict(self.includes)
        result += extract_includes(
            includes, lambda include, system: self.fname.endswith(replace_ext(include, '.cc')))
        result += extract_includes(includes, cpp_parser.is_c_system_header)
        result += extract_includes(includes, cpp_parser.is_cpp_system_header)
        result += extract_includes(includes, lambda include, system: system)
        prefix = None
        local_includes = extract_includes(includes, lambda include, system: True)
        for line in local_includes:
            if line == "":
                continue
            idx = line.find('/', line.find('/') + 1)
            cur_prefix = line[:idx] if idx != -1 else ""
            if prefix != cur_prefix:
                if prefix is not None:
                    result.append("")
                prefix = cur_prefix
            result.append(line)
        if len(local_includes) != 0:
            result.append("")
        i = 0
        while i < len(self.footer_lines) and self.footer_lines[i] == "":
            i += 1
        result += self.footer_lines[i:]
        return result


class SourceFile:
    def __init__(self, fname, node: typing.Union[Node, None]):
        self.fname = fname
        self.node = node
        self.lines = None
        self.modified_lines: typing.Dict[int, str] = {}
        self.checked: typing.Set[typing.Tuple[string, Step]] = set()
        self.unchecked: typing.List[CheckableInclude] = []
        self.running: typing.Union[CheckableInclude, None] = None
        self.includes: typing.Union[typing.List[cpp_parser.Include], None] = None
        self.step = Step.REMOVE
        self.injected_includes = set()

    def debug(self):
        return debug_filter_re is not None and debug_filter_re.search(self.fname)

    def debug_print(self, fmt, *args):
        if not self.debug():
            return
        print("{} {}:".format(self.fname, self.step), fmt.format(*args))

    def prepare(self):
        if self.lines is None:
            self.load_from_disk()
        if self.includes is None:
            return
        added_includes_set = set()
        added_includes_key = self.fname
        if added_includes_key.startswith('../../'):
            added_includes_key = added_includes_key[6:]
        if added_includes_key in added_includes:
            added_includes_set = added_includes[added_includes_key]
        for include in self.includes:
            cc_file = replace_ext(include.name, '.cc')
            if self.fname.endswith(cc_file):
                continue
            if include.name.endswith('_fwd.h') and \
                    os.path.dirname(self.fname).endswith(os.path.dirname(include.name)):
                self.debug_print("Skipping fwd header in the same dir: {}", include)
                continue
            if not include.trivial:
                self.debug_print("Skipping non trivial include: {}", include)
                continue
            if include.name.endswith('_fwd.h') and \
                    os.path.dirname(self.fname).endswith(os.path.dirname(include.name)):
                self.debug_print("Skipping fwd header in the same dir: {}", include)
                continue
            if not include.trivial:
                self.debug_print("Skipping non trivial include: {}", include)
                continue
            if (include.name, self.step) in self.checked or include.line_no in self.modified_lines:
                self.debug_print("Skipping checked include: {}", include)
                continue
            unique = True
            if include.gen_line() not in self.injected_includes and \
                    include.name not in added_includes_set:
                for other in self.includes:
                    if other.line_no == include.line_no or other.name not in nodes:
                        continue
                    refs = nodes[other.name].refs
                    if include.name in refs:
                        unique = False
                        break
            if not unique or include.name.startswith('boost/preprocessor') or \
                    include.name.startswith('yb/gutil/'):
                self.debug_print("Skipping non unique include: {}", include)
                continue
            if self.step == Step.INLINE:
                if include.system or include.name not in nodes or \
                        not nodes[include.name].parsed.trivial:
                    self.debug_print("Skipping non unique include: {}", include)
                    continue
            self.debug_print("Add: {}", include)
            self.unchecked.append(CheckableInclude(include.name, include.line_no, pick_task_id()))

    def load_from_disk(self):
        with open(self.fname) as inp:
            lines = inp.read().split('\n')
        parsed = cpp_parser.parse(self.fname, lines)
        if not parsed.trivial:
            return
        injected_includes = self.injected_includes
        for node in self.node.includes:
            injected_includes.update(node.removed_includes)
        for include in parsed.includes:
            line = include.gen_line()
            if line in injected_includes:
                injected_includes.remove(line)
        if len(injected_includes) != 0:
            print("Injecting to {}: {}".format(self.fname, injected_includes))
            for i in range(len(lines)):
                if lines[i].startswith("#include"):
                    generated_source = GeneratedSource(self.fname)
                    generated_source.extend(lines[:i])
                    generated_source.extend(injected_includes)
                    generated_source.extend(lines[i:])
                    lines = generated_source.complete()
                    parsed = None
                    break
        self.lines = lines
        if parsed is None:
            parsed = cpp_parser.parse(self.fname, self.lines)
        self.includes = parsed.includes

    def cleaned(self):
        for node in self.node.included_by:
            node.not_cleaned_includes -= 1

    def launch(self):
        result = self.perform()
        if result is None:
            self.cleaned()
        return result

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
        compdb_key = self.fname
        if is_header(self.fname):
            dirname = os.path.dirname(self.fname)
            if dirname == os.path.join(build_dir, 'src/yb/rocksdb'):
                dirname = os.path.join(dirname, 'db')
            compdb_key = os.path.join(dirname, header_cc)
        if compdb_key not in compdb:
            print("No compdb for {}/{}".format(self.fname, compdb_key))
            return None
        if isinstance(compdb[compdb_key], str):
            compdb[compdb_key] = shlex.split(compdb[compdb_key])
        source_name = self.temp_source('cc')

        for arg in compdb[compdb_key]:
            if replace is not None:
                command.append(replace)
                replace = None
                continue
            if arg == '-MT' or arg == '-o':
                replace = os.path.join(temp_dir, fname_for_task(self.running.task_id, 'o'))
            elif arg == '-MF':
                replace = os.path.join(temp_dir, fname_for_task(self.running.task_id, 'o.d'))
            elif arg == '-c':
                replace = source_name
            command.append(arg)
        print("Checking {}: {} {}".format(self.fname, self.step, self.running[0]), flush=True)
        inline = None if self.step == Step.REMOVE else self.running.name
        if is_header(self.fname):
            header_name = self.temp_source('h')
            self.write_source(header_name, (self.running.line_no, inline))
            with open(source_name, 'w') as out:
                out.write('#include "{}"\n'.format(header_name))
        else:
            self.write_source(source_name, (self.running.line_no, inline))
        return subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def temp_source(self, ext):
        return replace_ext(self.fname, '.' + fname_for_task(self.running.task_id, ext))

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
                    if include.trivial:
                        result.append(include.gen_line())
            p = modification[0] + 1
        result.extend(self.lines[p:])
        return result.complete()

    def write_source(self, fname, modification):
        new_lines = self.generate_lines(modification)
        with open(fname, 'w') as out:
            out.write('\n'.join(new_lines))

    def complete(self):
        if len(self.modified_lines) == 0:
            return self.clean_finished()
        for modified_line in self.modified_lines.keys():
            self.node.removed_includes.append(self.lines[modified_line])
        existing = set([x.name for x in self.includes])
        self.lines = self.generate_lines(None)
        parsed = cpp_parser.parse(self.fname, self.lines)
        if not parsed.trivial:
            print("{} became non trivial", self.fname)
        else:
            for include in parsed.includes:
                if include.name not in existing:
                    self.injected_includes.add(include.gen_line())
        self.includes = parsed.includes if parsed.trivial else None
        self.step = Step.REMOVE
        self.modified_lines.clear()
        return self.next_task()

    def clean_finished(self):
        if len(self.node.removed_includes) != 0 or len(self.injected_includes) != 0:
            has_not_injected = False
            for include in self.node.removed_includes:
                if include not in self.injected_includes:
                    has_not_injected = True
                    break
            if has_not_injected or len(self.injected_includes) > len(self.node.removed_includes):
                self.write_source(self.fname, None)
        self.cleaned()
        return None

    def next_task(self):
        result = self.perform()
        if result is None:
            return self.complete()
        return result

    def handle_result(self, return_code, output):
        os.unlink(self.temp_source('cc'))
        if is_header(self.fname):
            os.unlink(self.temp_source('h'))
        self.checked.add((self.running.name, self.step))
        if return_code == 0:
            print("Could {} {} from {}".format(self.step, self.running.name, self.fname))
            inline = None if self.step == Step.REMOVE else self.running.name
            self.modified_lines[self.running.line_no] = inline
        elif self.debug():
            self.debug_print("Failure {}:\n{}", self.running.name, output.decode("utf-8"))
        return self.next_task()


def process_prefix(fname, external):
    if external:
        for i in range(len(EXTERNAL_PREFIXES)):
            idx = fname.find(EXTERNAL_PREFIXES[i])
            if idx != -1:
                return i, idx + len(EXTERNAL_PREFIXES[i])

    else:
        for i in range(len(prefixes)):
            if fname.startswith(prefixes[i]):
                return i, len(prefixes[i])
    return None, None


def parse_comp_db():
    json_str = subprocess.check_output(['ninja', '-t', 'compdb'])
    compdb_json = json.loads(json_str)
    for entry in compdb_json:
        file = entry['file']
        command = entry['command']
        header_name = os.path.join(os.path.dirname(file), header_cc)
        compdb[file] = command
        if file.endswith('.cc') and header_name not in compdb[file]:
            compdb[header_name] = command


class RunningTask(typing.NamedTuple):
    file: SourceFile
    process: subprocess.Popen


def cleanup():
    pending_source = 0
    running_tasks: typing.List[RunningTask] = []
    while len(running_tasks) != 0 or len(sources) > pending_source:
        need_wait = True
        while len(running_tasks) < max_jobs and len(sources) > pending_source:
            idx = pending_source
            while idx < len(sources) and sources[idx].node.not_cleaned_includes != 0:
                idx += 1
            if idx == len(sources):
                node: Node = sources[pending_source].node
                print("Unable to clean {}, because of pending {} cleans".format(
                    node.fname, node.not_cleaned_includes))
                break
            if idx != pending_source:
                sources[idx], sources[pending_source] = sources[pending_source], sources[idx]
            source = sources[pending_source]
            process = source.launch()
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
            new_process = task.file.handle_result(return_code, None)  # task.process.stdout.read()
            if new_process is None:
                running_tasks[i] = running_tasks[len(running_tasks) - 1]
                running_tasks.pop()
            else:
                running_tasks[i] = RunningTask(task.file, new_process)
        if need_wait:
            time.sleep(0.1)


def source_file(fname: str):
    global filter_re
    if fname.startswith(build_dir):
        return False
    if filter_re is None:
        return True
    return filter_re.search(fname) is not None


def parse_deps():
    global nodes, sources
    with open('deps.txt') as inp:
        first = False
        for line in inp:
            line = line.rstrip()
            if not line.startswith(' '):
                first = True
                continue
            fname = line.lstrip()
            if first:
                if source_file(fname):
                    nodes[fname] = Node(fname, fname, False, -1)
                first = False
            elif fname.endswith(".h") or fname.endswith(".hpp") or \
                    fname.find('/usr/include/c++/v1/') != -1:
                external = not fname.startswith(root_dir) or \
                           fname.startswith(os.path.join(build_dir, 'postgres'))
                prefix_idx, prefix_len = process_prefix(fname, external)
                if prefix_idx is None:
                    error("Not found prefix for {}, external: {}", fname, external)
                ref_name = fname[prefix_len:]
                if ref_name not in nodes or prefix_idx < nodes[ref_name].prefix_idx:
                    nodes[ref_name] = Node(ref_name, fname, external, prefix_idx)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--filter')
    parser.add_argument('--debug-filter')
    parser.add_argument('--diff')
    return parser.parse_args()


def add_artificial_ref(file: str, ref: str):
    if file in nodes:
        nodes[file].refs.add(ref)


def parse_diff(fname):
    file_start_re = re.compile(R'^\+\+\+\sb/(.*)$')
    include_re = re.compile(R'^\+#include\s+["<](.*)[>"].*$')
    with open(fname) as inp:
        file = None
        for line in inp:
            line = line.rstrip()
            m = file_start_re.match(line)
            if m:
                file = m[1]
                added_includes[file] = set()
                continue
            m = include_re.match(line)
            if m:
                added_includes[file].add(m[1])


def main() -> None:
    global filter_re, debug_filter_re

    args = parse_args()

    if args.diff is not None:
        parse_diff(args.diff)

    print("Parsing compdb")
    parse_comp_db()
    print("Parsing deps")

    filter_re = re.compile(args.filter) if args.filter is not None else None
    debug_filter_re = re.compile(args.debug_filter) if args.debug_filter is not None else None

    parse_deps()

    # libc++ defines mutex and condition_variable in __mutex_base
    add_artificial_ref('__mutex_base', 'condition_variable')
    add_artificial_ref('__mutex_base', 'mutex')
    # libc++ declares basic_string in iterator
    add_artificial_ref('iterator', 'string')
    # libc++ declares vector in iosfwd
    add_artificial_ref('iosfwd', 'vector')

    # Old libstdc++ declares std::function in memory
    add_artificial_ref('memory', 'functional')

    for node in nodes.values():
        node.process()

    for source in sources:
        for node in source.node.included_by:
            node.not_cleaned_includes += 1

    with tempfile.TemporaryDirectory() as tmpdirname:
        global temp_dir
        temp_dir = tmpdirname
        print("Try cleanup {} sources".format(len(sources)))
        cleanup()


if __name__ == '__main__':
    main()
