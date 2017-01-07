#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

# Split a long line that is assumed to be a command line into multiple lines, so that the result
# could still be pasted into a terminal. The max line length could be approximate.

import sys

line_length = 0
buffer = ''
for line in sys.stdin:
    for c in line.rstrip():
        if c.isspace() and line_length > 80:
            print buffer + c + '\\\\'
            buffer = ''
            line_length = 0
        else:
            buffer += c
            line_length += 1

if buffer:
    print buffer,
