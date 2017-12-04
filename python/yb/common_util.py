"""
Copyright (c) YugaByte, Inc.

This module provides common utility functions.
"""

import itertools
import logging
import os


MODULE_DIR = os.path.dirname(os.path.realpath(__file__))
YB_SRC_ROOT = os.path.realpath(os.path.join(MODULE_DIR, '..', '..'))
YB_THIRDPARTY_DIR = os.environ.get("YB_THIRDPARTY_DIR", os.path.join(YB_SRC_ROOT, 'thirdparty'))


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
