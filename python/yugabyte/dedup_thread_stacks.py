#!/usr/bin/env python3

"""
Deduplicates similar stack frames. Used for post-processing thread stacks dumped from a core file.
"""

import re
import sys

from typing import List, Optional, Dict


THREAD_HEADER_RE = re.compile(r'^Thread (\d+) \(LWP (\d+)\):$')
HEX_NUMBER_RE_STR = r'\b0x[0-9a-fA-f]+\b'
STACK_FRAME_RE = re.compile(r'^#(\d+)\s+(.*)$')
PARAM_VALUE_RE = re.compile(r'=' + HEX_NUMBER_RE_STR)


class StackTrace:
    thread_id: int
    lwp_id: int
    frames: List[str]
    raw_frames: List[str]

    def __init__(self, thread_id: int, lwp_id: int) -> None:
        self.thread_id = thread_id
        self.lwp_id = lwp_id
        self.frames = []
        self.raw_frames = []

    def append_line(self, line: str) -> None:
        self.raw_frames.append(line)
        self.frames.append(line)

    def append_frame(self, index: int, line: str) -> bool:
        if index == len(self.frames):
            self.raw_frames.append(line)
            line = PARAM_VALUE_RE.sub('=0x...', line)
            self.frames.append(line)
            return True
        return False

    def frames_key(self) -> str:
        return "\n".join(self.frames)


class Collector:
    stacks: List[StackTrace]
    current_stack: Optional[StackTrace]

    def __init__(self) -> None:
        self.stacks = []
        self.current_stack = None

    def stack_finished(self) -> None:
        if self.current_stack is not None:
            self.stacks.append(self.current_stack)
            self.current_stack = None

    def process_line(self, line: str) -> bool:
        """
        :param line: a line from gdb output
        :return: True if the line was appended to the current stack trace, False if its format was
                 not recognized and the caller needs to handle (print) the line itself.
        """
        if line == '':
            if self.current_stack:
                self.current_stack.append_line(line)
                self.stack_finished()
                return True
            return False

        header_match = THREAD_HEADER_RE.match(line)
        if header_match:
            self.stack_finished()
            thread_id = int(header_match.group(1))
            lwp_id = int(header_match.group(2))
            self.current_stack = StackTrace(thread_id, lwp_id)
            return True

        frame_match = STACK_FRAME_RE.match(line)
        if frame_match:
            index = int(frame_match.group(1))
            if self.current_stack:
                return self.current_stack.append_frame(index, line)
            return False

        self.stack_finished()
        return False

    def print_grouped_stacks(self) -> None:
        groups: Dict[str, List[StackTrace]] = {}
        for stack in self.stacks:
            key = stack.frames_key()
            if key not in groups:
                groups[key] = []
            groups[key].append(stack)

        line_groups = []

        for key, stacks in groups.items():
            stacks = sorted(stacks, key=lambda stack: stack.thread_id)
            header = "Thread " + ", ".join(
                    "%d (LWP %d)" % (stack.thread_id, stack.lwp_id) for stack in stacks)
            min_thread_id = min(stack.thread_id for stack in stacks)
            if len(stacks) == 1:
                # This is a unique stack trace, so we can show argument values without introducing
                # any confusion.
                frames = stacks[0].raw_frames
            else:
                frames = stacks[0].frames

            line_groups.append((min_thread_id, [header] + frames))

        # Sort stack trace groups by the minimum thread id in the group.
        line_groups.sort(key=lambda t: t[0])
        for _, lines in line_groups:
            print("\n".join(lines))


if __name__ == '__main__':
    collector = Collector()

    for line in sys.stdin:
        line = line.rstrip()
        if not collector.process_line(line):
            print(line)

    collector.print_grouped_stacks()
