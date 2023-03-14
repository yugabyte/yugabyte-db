import os
import random
import re
import shlex
import statistics
import string
import subprocess
import time
import typing

import ninja


MASTER_CC_PATTERN = re.compile('(.*)/master\.cc(.*)')


def n2s(n):
    return int(n * 1000) / 1000


def generate_command(original_command: typing.List[str], fname: str) -> typing.List[str]:
    command: typing.List[str] = []
    for item in original_command:
        m = MASTER_CC_PATTERN.match(item)
        if m:
            command.append("{}/{}{}".format(m[1], fname, m[2]))
        else:
            command.append(item)
    command[0] = 'clang++'
    command.append('-ftime-trace')
    return command


def main() -> None:
    root_path = os.getcwd()
    build_path = os.readlink('build/latest')
    os.chdir(build_path)
    includes = []
    with open("inc.txt") as inp:
        for line in inp:
            tokens = line.rstrip().split(" ")
            count = None
            for token in tokens:
                if len(token) != 0:
                    count = int(token)
                    break
            fname = tokens[-1]
            if not fname.endswith(".h"):
                continue
            for prefix in [os.path.join(root_path, "src/"),
                           os.path.join(build_path, "src/")]:
                if fname.startswith(prefix):
                    includes.append((count, fname[len(prefix):]))
                    break

    includes.reverse()
    file = os.path.join(root_path, 'src/yb/master/master.cc')
    original_command: typing.List[str] = []
    for entry in ninja.parse_compdb():
        if entry.file == file:
            original_command = shlex.split(entry.command)
            break
    for (count, include) in includes:
        rname = ''.join(random.choices(string.ascii_letters + string.digits, k=8))
        iname = os.path.splitext(os.path.basename(include))[0]
        fname = rname + '.' + iname + '.cc'
        file_path = os.path.join("../../src/yb/master", fname)
        command = generate_command(original_command, fname)
        with open(file_path, 'w') as out:
            out.write("// " + shlex.join(command) + "\n")
            out.write('#include "{}"\n'.format(include))
        min_time = 1e9
        max_time = 0
        times = []
        msg = None
        iterations = 3
        for i in range(iterations):
            start = time.monotonic()
            res = subprocess.run(command, stderr=subprocess.DEVNULL)
            if res.returncode != 0:
                msg = "failed {}".format(res.returncode)
                break
            finish = time.monotonic()
            passed = finish - start
            if passed < min_time:
                min_time = passed
            if passed > max_time:
                max_time = passed
            times.append(passed)
        if msg is None:
            med_time = statistics.median(times)
            msg = 'min {} max {} med {}'.format(n2s(min_time), n2s(max_time), n2s(med_time))
            impact = med_time * count
        else:
            impact = -1
        print("{} {} {}: {}".format(n2s(impact), count, include, msg), flush=True)
        os.unlink(file_path)


if __name__ == '__main__':
    main()
