import logging
import re
import typing

IGNORE_RE = re.compile(
    R'^(?:\s*//.*|using\s.*|\s*#\s*pragma\s+once|\s*#\s*error.*)$')
IF_OPEN_RE = re.compile(R'^\s*#\s*if.*$')
IF_CLOSE_RE = re.compile(R'^\s*#\s*endif.*$')
ML_COMMENT_OPEN_RE = re.compile(R'^\s*/\*.*$')
ML_COMMENT_CLOSE_RE = re.compile(R'^.*\*/\s*$')
INCLUDE_RE = re.compile(R'^\s*#\s*include\s+"([^"]+)"(.*)$')
SYSTEM_INCLUDE_RE = re.compile(R'^\s*#\s*include\s+<([^>]+)>(.*)$')


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


def gen_line(include: str, system: bool):
    return "#include " + ('<{}>' if system else '"{}"').format(include)


class Include(typing.NamedTuple):
    name: str
    line_no: int
    system: bool
    trivial: bool

    def gen_line(self):
        return gen_line(self.name, self.system)

    def skip(self):
        if not self.trivial:
            return True
        return self.name.startswith("boost/preprocessor/") or self.name == 'yb/gutil/port.h'


# trivial means that #include directives do not interleave with other constructions.
class ParsedFile:
    def __init__(self):
        self.includes: typing.List[Include] = []
        self.trivial = True


class IncludeLine(typing.NamedTuple):
    file: str
    system: bool
    tail: str


def parse_include_line(line) -> typing.Union[None, IncludeLine]:
    match = INCLUDE_RE.match(line)
    system = False
    if not match:
        match = SYSTEM_INCLUDE_RE.match(line)
        system = True
    if not match:
        return None
    return IncludeLine(match[1], system, match[2])


# Parse specified file.
def parse(fname, lines) -> ParsedFile:
    header = not fname.endswith('.cc')
    line_no = -1
    result = ParsedFile()
    last_unknown = None
    if_nesting = 0
    ml_comment = False
    prev_lines = ['', '']
    for line in lines:
        line_no += 1
        line = line.rstrip()
        for i in range(len(prev_lines) - 1):
            prev_lines[i + 1] = prev_lines[i]
        prev_lines[0] = line
        if len(line) == 0 or IGNORE_RE.match(line):
            continue
        if not ml_comment and ML_COMMENT_OPEN_RE.match(line):
            ml_comment = True
        if ml_comment:
            if ML_COMMENT_CLOSE_RE.match(line):
                ml_comment = False
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
        parsed_include = parse_include_line(line)
        if parsed_include is not None:
            if result.trivial and last_unknown is not None:
                result.trivial = False
                logging.debug("{} non trivial because of {}".format(fname, last_unknown))
            if result.trivial and if_nesting != 0:
                result.trivial = False
            trivial = if_nesting == 0 and len(parsed_include.tail) == 0
            result.includes.append(
                Include(parsed_include.file, line_no, parsed_include.system, trivial))
        else:
            last_unknown = line
    return result


def parse_file(fname) -> ParsedFile:
    with open(fname) as inp:
        lines = inp.read().split('\n')
    return parse(fname, lines)


def include_prefix(include: str) -> str:
    idx = include.find('/')
    return "" if idx == -1 else include[:idx]


def is_c_system_header(header: str, system: bool) -> bool:
    return system and header.find(".") != -1 and (header not in lib_headers) and \
           (include_prefix(header) not in lib_header_prefixes)


def is_cpp_system_header(header: str, system: bool) -> bool:
    return system and header.find(".") == -1


def header_category(header: str, system: bool) -> int:
    if is_c_system_header(header, system):
        return 0
    elif is_cpp_system_header(header, system):
        return 1
    elif system:
        return 2
    else:
        return 3
