#!/usr/bin/env python

import os
import sys


if __name__ == '__main__':
    if os.path.realpath(sys.argv[1]) == os.path.realpath(sys.argv[2]):
        sys.exit(0)

    sys.exit(1)
