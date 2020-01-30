"""
Copyright (c) YugaByte, Inc.

This module provides common utility functions.
"""

import itertools
import logging
import os
import re
import sys


MODULE_DIR = os.path.dirname(os.path.realpath(__file__))
YB_SRC_ROOT = os.path.realpath(os.path.join(MODULE_DIR, '..', '..'))
NINJA_BUILD_ROOT_PART_RE = re.compile(r'-ninja($|(?=-))')

# We default this to the env var or to the actual local thirdparty call, but we expose a getter
# and setter below that should be imported and used to access this global.
_YB_THIRDPARTY_DIR = os.path.realpath(
        os.environ.get("YB_THIRDPARTY_DIR", os.path.join(YB_SRC_ROOT, 'thirdparty')))


def get_thirdparty_dir():
    global _YB_THIRDPARTY_DIR
    return _YB_THIRDPARTY_DIR


def set_thirdparty_dir(thirdparty_dir):
    global _YB_THIRDPARTY_DIR
    _YB_THIRDPARTY_DIR = thirdparty_dir
    os.environ["YB_THIRDPARTY_DIR"] = thirdparty_dir


def sorted_grouped_by(arr, key_fn):
    """
    Group the given collection by the key computed using the given function. The collection does not
    have to be already sorted.
    @return a list of (key, list_of_values) tuples where keys are sorted
    """
    return [(k, list(v)) for (k, v) in itertools.groupby(sorted(arr, key=key_fn), key_fn)]


def group_by(arr, key_fn):
    """
    Given a collection and a function that computes a key, returns a map from keys to all values
    with that key.
    """
    return dict(sorted_grouped_by(arr, key_fn))


def make_list(obj):
    """
    Convert the given object to a list. Strings get converted to a list of one string, not to a
    list of their characters.
    """
    if isinstance(obj, str):
        return [str]
    return list(obj)


def make_set(obj):
    return set(make_list(obj))


def init_env(verbose):
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s [%(filename)s:%(lineno)d %(levelname)s] %(message)s")


def get_build_type_from_build_root(build_root):
    return os.path.basename(build_root).split('-')[0]


def get_compiler_type_from_build_root(build_root):
    build_root_basename_components = os.path.basename(build_root).split('-')
    if len(build_root_basename_components) < 2:
        raise ValueError(
                "Too few components in build root basename: %s (build root: %s). "
                "Cannot get compiler type." % (build_root_basename_components, build_root))
    return build_root_basename_components[1]


def safe_path_join(*args):
    """Like os.path.join, but allows the first argument to be None."""
    if args[0] is None:
        return None
    return os.path.join(*args)


def set_to_comma_sep_str(items):
    if items:
        return ", ".join(sorted(set(items)))
    return "(empty)"


def convert_to_non_ninja_build_root(build_root):
    """
    >>> convert_to_non_ninja_build_root('/foo/bar-ninja-bar/yugabyte/build/some-build-ninja')
    '/foo/bar-ninja-bar/yugabyte/build/some-build'
    >>> convert_to_non_ninja_build_root('/foo/bar-ninja-bar/yugabyte/build/some-build-ninja-foo')
    '/foo/bar-ninja-bar/yugabyte/build/some-build-foo'
    """
    directory = os.path.dirname(build_root)
    basename = os.path.basename(build_root)
    return os.path.join(directory, NINJA_BUILD_ROOT_PART_RE.sub('', basename))


def is_ninja_build_root(build_root):
    return build_root != convert_to_non_ninja_build_root(build_root)


def get_bool_env_var(env_var_name):
    value = os.environ.get(env_var_name, None)
    if value is None:
        return False

    return value.lower() in ['1', 't', 'true', 'y', 'yes']


def is_yb_src_root(d):
    for subdir in ['.git', 'src', 'java', 'bin', 'build-support']:
        if not os.path.exists(os.path.join(d, subdir)):
            return False
    return True


def get_yb_src_root_from_build_root(build_dir, verbose=False, must_succeed=False):
    """
    Given a build directory, find the YB Git repository's root corresponding to it.
    """
    yb_src_root = None
    current_dir = build_dir
    while current_dir != '/':
        subdir_candidates = [current_dir]
        # Handle the "external build" directory case, in which the code is located in e.g.
        # ~/code/yugabyte, and the build directories are inside ~/code/yugabyte__build.
        if current_dir.endswith('__build'):
            subdir_candidates.append(current_dir[:-7])
            if subdir_candidates[-1].endswith('/'):
                subdir_candidates = subdir_candidates[:-1]

        for git_subdir_candidate in subdir_candidates:
            git_dir_candidate = os.path.join(current_dir, git_subdir_candidate)
            if is_yb_src_root(git_dir_candidate):
                yb_src_root = git_dir_candidate
                break
        if yb_src_root:
            break
        current_dir = os.path.dirname(current_dir)

    if yb_src_root:
        if verbose:
            logging.info(
                    "Found YB git repository root %s corresponding to the build directory %s",
                    yb_src_root, build_dir)
    else:
        error_msg = "Could not find git repository root by walking up from %s" % build_dir
        logging.warn(error_msg)
        if must_succeed:
            raise RuntimeError(error_msg)

    return yb_src_root


def is_macos():
    return sys.platform == 'darwin'
