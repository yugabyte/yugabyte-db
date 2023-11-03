#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
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
This module provides an iterator to simplify iteration over log records.
"""

from datetime import datetime
from enum import Enum
import re
from typing import Iterator, List, NamedTuple


START_LOG_REGEX = re.compile(r'''
    ^
    (?P<level>[IWEF])
    (?P<month>\d{2}) (?P<day>\d{2})
    \s
    (?P<hour>\d{2}) : (?P<minute>\d{2}) : (?P<second>\d{2}) \. (?P<microsecond>\d{6})
    \s+
    (?P<pid>\d+)
    \s+
    (?P<file>[^:]+) : (?P<line>\d+)
    \] \s (?P<message>.*)
    $
''', re.VERBOSE)


class LogLevel(Enum):
    INFO = 0
    WARNING = 1
    ERROR = 2
    FATAL = 3

    @staticmethod
    def from_first_char(c: str) -> 'LogLevel':
        if c == 'I':
            return LogLevel.INFO
        elif c == 'W':
            return LogLevel.WARNING
        elif c == 'E':
            return LogLevel.ERROR
        elif c == 'F':
            return LogLevel.FATAL
        raise ValueError(f"Unknown log level '{c}'")


class LogEntry(NamedTuple):
    level: LogLevel
    time: datetime
    pid: int
    file: str
    line: int
    message_lines: List[str]

    @property
    def message(self) -> str:
        return '\n'.join(self.message_lines)


def LogIterator(line_iter: Iterator[str]) -> Iterator[LogEntry]:
    this_year = datetime.now().year

    # Skip to start of first log entry.
    while True:
        line = next(line_iter)
        first_line_match = START_LOG_REGEX.match(line)
        if first_line_match:
            break

    while first_line_match:
        message_lines = [first_line_match.group('message')]
        try:
            while True:
                line = next(line_iter)
                next_match = START_LOG_REGEX.match(line)
                if next_match:
                    break
                message_lines.append(line.rstrip())
        except StopIteration:
            next_match = None

        yield LogEntry(
            level=LogLevel.from_first_char(first_line_match.group('level')),
            time=datetime(
                this_year,
                int(first_line_match.group('month')),
                int(first_line_match.group('day')),
                int(first_line_match.group('hour')),
                int(first_line_match.group('minute')),
                int(first_line_match.group('second')),
                int(first_line_match.group('microsecond')),
            ),
            pid=int(first_line_match.group('pid')),
            file=first_line_match.group('file'),
            line=int(first_line_match.group('line')),
            message_lines=message_lines)

        first_line_match = next_match
