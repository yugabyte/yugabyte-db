#!/usr/bin/env python

# Get the relative path for a source file. Relative to:
# - source root (YB_SRC_ROOT)
# - build root (YB_BUILD_ROOT)


import os
import sys


if __name__ == '__main__':
    file_path = os.path.realpath(sys.argv[1])
    base_path = None
    for base_path_env_var in ['YB_BUILD_ROOT', 'YB_SRC_ROOT']:
        base_path_candidate = os.path.realpath(os.getenv(base_path_env_var))
        if file_path.startswith(base_path_candidate + '/'):
            base_path = base_path_candidate
            break
    if base_path is not None:
        file_path = os.path.relpath(file_path, base_path)
    print(file_path)
