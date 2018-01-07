"""
Copyright (c) YugaByte, Inc.

This module provides common utility functions.
"""

import itertools
import logging
import os
import re


MODULE_DIR = os.path.dirname(os.path.realpath(__file__))
YB_SRC_ROOT = os.path.realpath(os.path.join(MODULE_DIR, '..', '..'))
YB_THIRDPARTY_DIR = os.path.realpath(
    os.environ.get("YB_THIRDPARTY_DIR", os.path.join(YB_SRC_ROOT, 'thirdparty')))
NINJA_BUILD_ROOT_PART_RE = re.compile(r'-ninja($|(?=-))')


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


def get_bool_env_var(env_var_name):
    value = os.environ.get(env_var_name, None)
    if value is None:
        return False

    return value.lower() in ['1', 't', 'true', 'y', 'yes']
