# Copyright (c) Yugabyte, Inc.
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

"""
This module provides common utility functions.
"""

import atexit
import itertools
import logging
import os
import re
import shutil
import subprocess
import shlex
import io
import platform
import argparse
import uuid
import pathlib
import random

import typing
from typing import (
    Any, List, Tuple, Dict, Callable, TypeVar, Union, Set, Optional, cast, Iterable, TYPE_CHECKING)


if TYPE_CHECKING:
    class _SupportsLessThan(typing.Protocol):
        def __lt__(self, __other: Any) -> bool: ...

    _SupportsLessThanT = TypeVar("_SupportsLessThanT", bound=_SupportsLessThan)
    _GroupElementType = TypeVar('_GroupElementType')
    _GroupKeyType = TypeVar('_GroupKeyType', bound=_SupportsLessThan)
else:
    # A workaround for supporting Python 3.7 because it does not have typing.Protocol.
    # This part should be unnecessary with Python 3.8.
    _GroupElementType = typing.Any
    _GroupKeyType = typing.Any


MODULE_DIR = os.path.dirname(os.path.realpath(__file__))
YB_SRC_ROOT = os.path.realpath(os.path.join(MODULE_DIR, '..', '..'))
NINJA_BUILD_ROOT_PART_RE = re.compile(r'-ninja($|(?=-))')
JSON_INDENTATION = 2

# We default this to the env var or to the actual local thirdparty call, but we expose a getter
# and setter below that should be imported and used to access this global.
_YB_THIRDPARTY_DIR = os.path.realpath(
        os.environ.get("YB_THIRDPARTY_DIR", os.path.join(YB_SRC_ROOT, 'thirdparty')))

GLOBAL_DOWNLOAD_CACHE_DIR = '/opt/yb-build/download_cache'
attempted_to_create_download_cache_dir = False


def get_thirdparty_dir() -> str:
    global _YB_THIRDPARTY_DIR
    return _YB_THIRDPARTY_DIR


def set_thirdparty_dir(thirdparty_dir: str) -> None:
    global _YB_THIRDPARTY_DIR
    _YB_THIRDPARTY_DIR = thirdparty_dir
    os.environ["YB_THIRDPARTY_DIR"] = thirdparty_dir


def sorted_grouped_by(
        arr: Iterable[_GroupElementType],
        key_fn: Callable[[_GroupElementType], _GroupKeyType]
        ) -> List[Tuple[_GroupKeyType, List[_GroupElementType]]]:
    """
    Group the given collection by the key computed using the given function. The collection does not
    have to be already sorted.
    @return a list of (key, list_of_values) tuples where keys are sorted
    """
    return [(k, list(v)) for (k, v) in itertools.groupby(sorted(arr, key=key_fn), key_fn)]


def group_by(
        arr: Iterable[_GroupElementType],
        key_fn: Callable[[_GroupElementType], _GroupKeyType]
        ) -> Dict[_GroupKeyType, List[_GroupElementType]]:
    """
    Given a collection and a function that computes a key, returns a map from keys to all values
    with that key.
    """
    return dict(sorted_grouped_by(arr, key_fn))


def make_list(obj: Union[str, List[Any]]) -> List[Any]:
    """
    Convert the given object to a list. Strings get converted to a list of one string, not to a
    list of their characters.
    """
    if isinstance(obj, str):
        return [str]
    return list(obj)


def make_set(obj: Union[str, List[Any]]) -> Set[Any]:
    return set(make_list(obj))


g_init_logging_verbose_value: Optional[bool] = None


def init_logging(verbose: bool) -> None:
    global g_init_logging_verbose_value

    if g_init_logging_verbose_value is not None:
        # This function has already been called.
        if verbose == g_init_logging_verbose_value:
            # The same value of the verbose argument, skip.
            return
        logging.warning(
            f"init_logging() has already been called with verbose={g_init_logging_verbose_value}, "
            f"ignoring a subsequent call with verbose={verbose}"
        )
        return

    g_init_logging_verbose_value = verbose

    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s [%(filename)s:%(lineno)d %(levelname)s] %(message)s")


def get_build_type_from_build_root(build_root: str) -> str:
    return os.path.basename(build_root).split('-')[0]


def is_lto_build_root(build_root: str) -> bool:
    """
    >>> is_lto_build_root('build/release-clang14-full-lto-ninja')
    True
    >>> is_lto_build_root('build/debug-clang13-dynamic-ninja')
    False
    """
    build_root_base_name = os.path.basename(build_root)
    return '-thin-lto-' in build_root_base_name or '-full-lto' in build_root_base_name


def get_compiler_type_from_build_root(build_root: str) -> str:
    build_root_basename_components = os.path.basename(build_root).split('-')
    if len(build_root_basename_components) < 2:
        raise ValueError(
                "Too few components in build root basename: %s (build root: %s). "
                "Cannot get compiler type." % (build_root_basename_components, build_root))
    return build_root_basename_components[1]


def set_env_vars_from_build_root(build_root: str) -> None:
    build_root_from_env = os.getenv('BUILD_ROOT')
    if build_root_from_env is not None and build_root_from_env != build_root:
        raise ValueError(
            "The BUILD_ROOT environment variable is %s but the build root is being set to %s" % (
                build_root_from_env, build_root))
    os.environ['BUILD_ROOT'] = build_root

    compiler_type_from_build_root = get_compiler_type_from_build_root(build_root)
    compiler_type_from_env = os.getenv('YB_COMPILER_TYPE')
    if (compiler_type_from_env is not None and
            compiler_type_from_env != compiler_type_from_build_root):
        raise ValueError(
            "The YB_COMPILER_TYPE environment variable is %s but the compiler type derived "
            "from the build root is %s" % (compiler_type_from_env, compiler_type_from_build_root))
    os.environ['YB_COMPILER_TYPE'] = compiler_type_from_build_root


def safe_path_join(args: List[Optional[str]]) -> Optional[str]:
    """Like os.path.join, but allows the first argument to be None."""
    if args[0] is None:
        return None
    if any(arg is None for arg in args):
        raise ValueError(f"Only the first argument is allowed to be None, got {args}")
    return os.path.join(*[cast(str, arg) for arg in args])


def set_to_comma_sep_str(items: Iterable[str]) -> str:
    if items:
        return ", ".join(sorted(set(items)))
    return "(empty)"


def convert_to_non_ninja_build_root(build_root: str) -> str:
    """
    >>> convert_to_non_ninja_build_root('/foo/bar-ninja-bar/yugabyte/build/some-build-ninja')
    '/foo/bar-ninja-bar/yugabyte/build/some-build'
    >>> convert_to_non_ninja_build_root('/foo/bar-ninja-bar/yugabyte/build/some-build-ninja-foo')
    '/foo/bar-ninja-bar/yugabyte/build/some-build-foo'
    """
    directory = os.path.dirname(build_root)
    basename = os.path.basename(build_root)
    return os.path.join(directory, NINJA_BUILD_ROOT_PART_RE.sub('', basename))


def is_ninja_build_root(build_root: str) -> bool:
    return build_root != convert_to_non_ninja_build_root(build_root)


def get_bool_env_var(env_var_name: str) -> bool:
    value = os.environ.get(env_var_name, None)
    if value is None:
        return False

    return value.lower() in ['1', 't', 'true', 'y', 'yes']


def is_yb_src_root(candidate_dir_path: str) -> bool:
    for subdir in ['.git', 'src', 'java', 'bin', 'build-support']:
        if not os.path.exists(os.path.join(candidate_dir_path, subdir)):
            return False
    return True


def get_yb_src_root_from_build_root(
        build_dir: str,
        verbose: bool = False,
        must_succeed: bool = False) -> Optional[str]:
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


def ensure_yb_src_root_from_build_root(build_dir: str, verbose: bool = False) -> str:
    yb_src_root = get_yb_src_root_from_build_root(
        build_dir=build_dir, verbose=verbose, must_succeed=True)
    assert yb_src_root is not None
    return yb_src_root


def is_macos() -> bool:
    return platform.system() == 'Darwin'


def get_absolute_path_aliases(path: str) -> List[str]:
    """
    Returns a list of different variants (just an absolute path vs. all symlinks resolved) for the
    given path.
    """
    return sorted(set([os.path.abspath(path), os.path.realpath(path)]))


def find_executable(rel_path: str, must_find: bool = False) -> Optional[str]:
    """
    Similar to the UNIX "which" command.
    """
    if os.path.isabs(rel_path):
        raise ValueError("Expected an absolute path, got: %s", rel_path)
    path_env_var = os.getenv('PATH')
    if path_env_var is None:
        raise ValueError("The PATH environment variable is not set")
    for search_dir in path_env_var.split(os.path.pathsep):
        joined_path = os.path.join(search_dir, rel_path)
        if os.path.exists(joined_path) and os.access(joined_path, os.X_OK):
            return joined_path
    if must_find:
        raise IOError("Could not find executable %s. PATH: %s" % (rel_path, path_env_var))
    return None


def rm_rf(path: str) -> None:
    if path == '/':
        raise ValueError("Cannot remove directory recursively: %s", path)
    if os.path.isabs(path):
        raise ValueError("Absolute path required, got %s", path)

    subprocess.check_call(['rm', '-rf', path])


def shlex_join(args: List[str]) -> str:
    # In Python 3.8 we can use shlex.join instead of this.
    return ' '.join([shlex.quote(arg) for arg in args])


def check_call_and_log(args: List[str]) -> None:
    cmd_str = shlex_join(args)
    logging.info("Running command: %s", cmd_str)
    try:
        subprocess.check_call(args)
    except subprocess.CalledProcessError as ex:
        logging.exception("Command failed with exit code %d: %s", ex.returncode, cmd_str)
        raise ex


def set_or_del_env_var(k: str, v: Optional[str]) -> None:
    """
    Set the value of the given environment variable to the given value, or delete it if the value
    is None.
    """
    if v is None:
        if k in os.environ:
            del os.environ[k]
    else:
        os.environ[k] = v


class EnvVarContext:
    """
    Sets the given environment variables and restores them on exit. A None value means the variable
    is undefined.
    """
    env_vars: Dict[str, Optional[str]]

    def __init__(self, **env_vars: Optional[str]) -> None:
        self.env_vars = env_vars

    def __enter__(self) -> 'EnvVarContext':
        self.saved_env_vars = {}
        for env_var_name, new_value in self.env_vars.items():
            self.saved_env_vars[env_var_name] = os.environ.get(env_var_name)
            set_or_del_env_var(env_var_name, new_value)
        return self

    # TODO: more precise types for arguments.
    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        for env_var_name, saved_value in self.saved_env_vars.items():
            set_or_del_env_var(env_var_name, saved_value)


def get_download_cache_dir() -> str:
    global attempted_to_create_download_cache_dir
    if not os.path.exists(GLOBAL_DOWNLOAD_CACHE_DIR) and not attempted_to_create_download_cache_dir:
        attempted_to_create_download_cache_dir = True
        try:
            original_umask = os.umask(0)
            os.makedirs(GLOBAL_DOWNLOAD_CACHE_DIR, 0o777)
        except Exception as ex:
            logging.exception(f"Could not create directory {GLOBAL_DOWNLOAD_CACHE_DIR}")
        finally:
            os.umask(original_umask)
    if os.path.isdir(GLOBAL_DOWNLOAD_CACHE_DIR) and os.access(GLOBAL_DOWNLOAD_CACHE_DIR, os.W_OK):
        return GLOBAL_DOWNLOAD_CACHE_DIR
    return os.path.expanduser('~/.cache/downloads')


def get_ruamel_yaml_instance() -> Any:
    # Import the ruamel.yaml module locally so that the common_util module is still usable outside
    # of any virtualenv.
    import ruamel.yaml  # type: ignore
    yaml = ruamel.yaml.YAML()
    yaml.indent(sequence=4, offset=2)
    return yaml


def load_yaml_file(yaml_path: str) -> Any:
    yaml = get_ruamel_yaml_instance()
    with open(yaml_path) as yaml_file:
        return yaml.load(yaml_file)


def write_yaml_file(content: Any, output_file_path: str) -> None:
    yaml = get_ruamel_yaml_instance()
    with open(output_file_path, 'w') as output_file:
        yaml.dump(content, output_file)


def make_parent_dir(path: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)


def to_yaml_str(content: Any) -> str:
    out = io.StringIO()
    yaml = get_ruamel_yaml_instance()
    yaml.dump(content, out)
    return out.getvalue()


def get_target_arch() -> str:
    return os.environ['YB_TARGET_ARCH']


def check_arch() -> None:
    if not is_macos():
        return
    target_arch = get_target_arch()
    actual_arch = platform.machine()
    if target_arch and actual_arch != target_arch:
        raise ValueError(
                f"YB_TARGET_ARCH is set to {target_arch} but we are running on the "
                f"{actual_arch} architecture.")


def is_macos_arm64() -> bool:
    return is_macos() and get_target_arch() == 'arm64'


# From https://stackoverflow.com/questions/15008758/parsing-boolean-values-with-argparse
# To be used for boolean arguments.
def arg_str_to_bool(v: Any) -> bool:
    if isinstance(v, bool):
        return v
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    if v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    raise argparse.ArgumentTypeError('Boolean value expected, got: %s' % v)


g_home_dir: Optional[str] = None
g_home_dir_aliases: Set[str] = set()


def get_home_dir() -> str:
    global g_home_dir, g_home_dir_aliases
    if g_home_dir is not None:
        return g_home_dir

    home_dir_raw = os.path.expanduser('~')
    home_dir_realpath = os.path.realpath(os.path.expanduser('~'))
    g_home_dir = home_dir_realpath
    g_home_dir_aliases = {home_dir_raw, home_dir_realpath}
    return g_home_dir


def get_home_dir_aliases() -> Set[str]:
    """
    Returns aliases for the home directory (the home directory itself and its real path).
    """
    global g_home_dir_aliases
    if not g_home_dir_aliases:
        get_home_dir()
    assert g_home_dir_aliases
    return g_home_dir_aliases


def get_relative_path_or_none(abs_path: str, relative_to: str) -> Optional[str]:
    """
    If the given path starts with another directory path, return the relative path, or else None.
    """
    if not relative_to.endswith('/'):
        relative_to += '/'
    if abs_path.startswith(relative_to):
        return abs_path[len(relative_to):]
    return None


K = TypeVar('K')
V = TypeVar('V')


def append_to_list_in_dict(dest: Dict[K, List[V]], key: K, new_item: V) -> None:
    """
    Append the given element to the list that the given key in the given dict points to. If the key
    does not point to anything, create a new list at that key.
    """
    if key in dest:
        dest[key].append(new_item)
    else:
        dest[key] = [new_item]


def optional_message_to_prefix(message: str) -> str:
    message = message.strip()
    if message == '':
        return ''
    if message.endswith((':', '.')):
        return message + ' '
    return message + ': '


def set_to_str_one_element_per_line(s: Set[Any]) -> str:
    '''
    >>> print(set_to_str_one_element_per_line({1}))
    {1}
    >>> print(set_to_str_one_element_per_line({1, 2}))
    {
        1,
        2
    }
    >>> print(set_to_str_one_element_per_line({'a', 'b', 'c'}))
    {
        a,
        b,
        c
    }
    '''
    if not s:
        return '{}'
    elements = sorted(s)
    if len(elements) == 1:
        return '{%s}' % str(elements[0])
    return '{\n    %s\n}' % (
        ',\n    '.join(
            [str(element) for element in elements]
        )
    )


def assert_sets_equal(a: Set[Any], b: Set[Any], message: str = '') -> None:
    a = set(a)
    b = set(b)
    if a != b:
        raise AssertionError(
            f"{optional_message_to_prefix(message)}"
            f"Sets are not equal: {set_to_str_one_element_per_line(a)} vs. "
            f"{set_to_str_one_element_per_line(b)}. "
            f"Elements only in the first set: {set_to_str_one_element_per_line(a - b)}. "
            f"Elements only in the second set: {set_to_str_one_element_per_line(b - a)}.")


def assert_set_contains_all(a: Set[Any], b: Set[Any], message: str = '') -> None:
    a = set(a)
    b = set(b)
    d = b - a
    if d:
        raise AssertionError(
            f"{optional_message_to_prefix(message)}"
            f"First set does not contain the second: {set_to_str_one_element_per_line(a)} vs. "
            f"{set_to_str_one_element_per_line(b)}. "
            f"Elements in the second set but not in the first: "
            f"{set_to_str_one_element_per_line(d)}.")


def create_temp_dir(keep_tmp_dir: bool = False) -> str:
    """
    Creates a temporary directory inside the "build" directory and returns its path.

    :param keep_tmp_dir: If true, the temporary directory will not be deleted at exit.
    """

    tmp_dir = os.path.join(
        YB_SRC_ROOT,
        "build",
        "yb_tmp_{}_{}".format(
            str(uuid.uuid4()),
            ''.join([str(random.randint(0, 9)) for _ in range(16)])
        )
    )
    try:
        pathlib.Path(tmp_dir).mkdir(parents=True, exist_ok=True)
    except OSError as e:
        logging.error("Could not create directory at '{}'".format(tmp_dir))
        raise e
    if not keep_tmp_dir:
        atexit.register(lambda: shutil.rmtree(tmp_dir))
    return tmp_dir


def create_symlink(src: str, dst: str) -> None:
    logging.info("Creating symbolic link %s -> %s", dst, src)
    if os.path.islink(dst):
        existing_link_src = os.readlink(dst)
        if existing_link_src == src:
            logging.info("Symlink %s already exists and points to %s", dst, src)
            return
        raise ValueError(
                "Symlink %s already exists and points to %s instead of %s" % (
                    dst, existing_link_src, src))
    os.symlink(src, dst)


def ensure_starts_with(s: str, prefix: str) -> str:
    """
    >>> ensure_starts_with('foo', 'a')
    'afoo'
    >>> ensure_starts_with('foo', 'f')
    'foo'
    """
    if not s.startswith(prefix):
        return prefix + s
    return s


def ensure_ends_with(s: str, suffix: str) -> str:
    """
    >>> ensure_ends_with('foo', 'f')
    'foof'
    >>> ensure_ends_with('foo', 'o')
    'foo'
    """
    if not s.endswith(suffix):
        return s + suffix
    return s


def ensure_enclosed_by(s: str, prefix: str, suffix: str) -> str:
    """
    >>> ensure_enclosed_by('abc', '{', '}')
    '{abc}'
    >>> ensure_enclosed_by('<abc', '<', '>')
    '<abc>'
    >>> ensure_enclosed_by('abc]', '[', ']')
    '[abc]'
    """
    return ensure_ends_with(ensure_starts_with(s, prefix), suffix)


def are_files_equal(path1: str, path2: str) -> bool:
    size1 = os.path.getsize(path1)
    size2 = os.path.getsize(path2)
    if size1 != size2:
        return False
    with open(path1, "rb") as file1, open(path2, "rb") as file2:
        buf_size = 512 * 1024
        while True:
            chunk1 = file1.read(buf_size)
            chunk2 = file2.read(buf_size)
            if chunk1 != chunk2:
                return False
            if len(chunk1) == 0 and len(chunk2) == 0:
                return True


def join_paths_if_needed(base_path: str, abs_or_rel_path: str) -> str:
    """
    If the given path is absolute, return it. Otherwise, join it with the given base path and return
    the result.
    >>> join_paths_if_needed('/a/b', '/c/d')
    '/c/d'
    >>> join_paths_if_needed('/a/b', 'c/d')
    '/a/b/c/d'
    """
    if os.path.isabs(abs_or_rel_path):
        return abs_or_rel_path
    return os.path.join(base_path, abs_or_rel_path)
