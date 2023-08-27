#
# Copyright (c) YugaByte, Inc.
# # Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
"""
Combs a log generated with tcmalloc_trace_enabled and provides statistics about each stack
trace found.
"""

import argparse
import itertools
import re
import sys
from typing import Dict, Iterator, List

from yugabyte.log.log_iter import LogIterator


MALLOC_CALL_RE = re.compile(r'^Malloc Call: size = (?P<size>\d+)$')

EXCLUDED_CALLS = [
    r' MallocHook::.*',
    r' tcmalloc::*',
]

EXCLUDED_CALL_RE = [re.compile(regex) for regex in EXCLUDED_CALLS]


def check_excluded(s: str) -> bool:
    return any(r.search(s) for r in EXCLUDED_CALL_RE)


def get_malloc_trace_dict(lines: Iterator[str]) -> Dict[str, List[int]]:
    stacks: Dict[str, List[int]] = {}

    for log in LogIterator(lines):
        if len(log.message_lines) < 2:
            continue
        first_line = log.message_lines[0]

        match = MALLOC_CALL_RE.match(first_line)
        if match:
            stack_trace = '\n'.join(line for line in log.message_lines if not check_excluded(line))
            if stack_trace not in stacks:
                stacks[stack_trace] = []
            stacks[stack_trace].append(int(match.group('size')))

    return stacks


def print_malloc_trace_summary(trace_dict: Dict[str, List[int]], limit: int = -1) -> None:
    trace_stats = sorted([
        (trace, sum(sizes), len(sizes), min(sizes), max(sizes))
        for trace, sizes in trace_dict.items()
    ], key=lambda x: -x[1])

    overall_total_alloc = sum(x[1] for x in trace_stats)
    print(f'TOTAL: {overall_total_alloc}\n')

    limit = limit if limit != -1 else len(trace_stats)
    for stack, total_alloc, num_alloc, min_alloc, max_alloc in trace_stats[:limit]:
        avg_alloc = total_alloc / num_alloc
        print(
            f'SIZE: {total_alloc} over {num_alloc} allocations '
            f'(min {min_alloc}, avg {avg_alloc}, max {max_alloc})\n'
            f'{stack}\n')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--limit',
        help='Max number of stack traces to output.',
        type=int,
        default=-1)
    parser.add_argument(
        'files',
        help='Log files to scan through. Reads from stdin if not specified.',
        nargs='*')
    args = parser.parse_args()

    files = (open(file, 'rt') for file in args.files) if args.files else [sys.stdin]
    lines = itertools.chain.from_iterable(files)
    print_malloc_trace_summary(get_malloc_trace_dict(lines), args.limit)


if __name__ == '__main__':
    main()
