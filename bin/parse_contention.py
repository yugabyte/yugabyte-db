#! /bin/python

import os
import re
import sys


class Stats():
    def __init__(self):
        self.total_time = 0
        self.counts = 0
        self.stack_traces = []
        self.started = False
        self.ended = False

    def cost(self):
        return self.total_time / self.counts

    def _relevant_trace(self):
        return self.stack_traces[1] if len(self.stack_traces) > 2 else None

    def process_line(self, line):
        if self.ended:
            return
        if "---" in line:
            self.ended = True
            return
        result = re.match("^([0-9]+)\s+([0-9]+)\s+@.*$", line)
        if result:
            self.total_time = int(result.group(1))
            self.counts = int(result.group(2))
            self.started = True
            return
        if self.started:
            self.stack_traces.append(line.strip('\n'))


def process_stats(stats):
    stats = [s for s in stats if s._relevant_trace()]
    stats = sorted(stats, key=lambda s: s.cost(), reverse=True)
    for s in stats:
        print("Cost: {:<20}\nTotal time: {:<20}\nNumber of calls: {:<20}\n{}".format(
              s.cost(), s.total_time, s.counts, "\n".join(s.stack_traces)))


def main():
    if len(sys.argv) != 2:
        print("Usage: {} <contention.txt>".format(sys.argv[0]))
        sys.exit(1)
    stats = []
    with open(sys.argv[1], "rb") as f:
        s = Stats()
        while True:
            line = f.readline()
            if not line:
                break
            s.process_line(line)
            if s.ended:
                stats.append(s)
                s = Stats()
        if s.started:
            stats.append(s)
    process_stats(stats)


if __name__ == "__main__":
    main()
