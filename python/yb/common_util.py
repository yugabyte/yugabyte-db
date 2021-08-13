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

import itertools
import logging
import os
import re
import sys
import json
import subprocess
import shlex
import io

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
        arr: List[_GroupElementType],
        key_fn: Callable[[_GroupElementType], _GroupKeyType]
        ) -> List[Tuple[_GroupKeyType, List[_GroupElementType]]]:
    """
    Group the given collection by the key computed using the given function. The collection does not
    have to be already sorted.
    @return a list of (key, list_of_values) tuples where keys are sorted
    """
    return [(k, list(v)) for (k, v) in itertools.groupby(sorted(arr, key=key_fn), key_fn)]


def group_by(
        arr: List[_GroupElementType],
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


def init_env(verbose: bool) -> None:
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s [%(filename)s:%(lineno)d %(levelname)s] %(message)s")


def get_build_type_from_build_root(build_root: str) -> str:
    return os.path.basename(build_root).split('-')[0]


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


def is_macos() -> bool:
    return sys.platform == 'darwin'


def write_json_file(
        json_data: Any, output_path: str, description_for_log: Optional[str] = None) -> None:
    with open(output_path, 'w') as output_file:
        json.dump(json_data, output_file, indent=JSON_INDENTATION)
        if description_for_log is not None:
            logging.info("Wrote %s: %s", description_for_log, output_path)


def read_json_file(input_path: str) -> Any:
    try:
        with open(input_path) as input_file:
            return json.load(input_file)
    except:  # noqa: E129
        # We re-throw the exception anyway.
        logging.error("Failed reading JSON file %s", input_path)
        raise


def read_file(file_path: str) -> str:
    with open(file_path) as input_file:
        return input_file.read()


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


def to_yaml_str(content: Any) -> str:
    out = io.StringIO()
    yaml = get_ruamel_yaml_instance()
    yaml.dump(content, out)
    return out.getvalue()
