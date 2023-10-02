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

import logging
import os

from yugabyte.common_util import get_home_dir_aliases, YB_SRC_ROOT
from typing import Dict, List, Any, Optional, cast


def replace_home_dir_with_tilde(p: str) -> str:
    """
    Transforms a path before logging by replacing the home directory path with ~.

    >>> replace_home_dir_with_tilde(os.path.expanduser('~/foo'))
    '~/foo'
    >>> replace_home_dir_with_tilde(os.path.expanduser('~'))
    '~'
    >>> replace_home_dir_with_tilde(os.path.realpath(os.path.expanduser('~')))
    '~'
    >>> replace_home_dir_with_tilde('/usr/bin')
    '/usr/bin'
    """
    for home_dir in get_home_dir_aliases():
        if p == home_dir:
            return '~'

        home_dir_prefix = home_dir + '/'
        if p.startswith(home_dir_prefix):
            return '~/%s' % p[len(home_dir_prefix):]

    return p


class LogArgRewriter:
    path_to_var_name: Dict[str, str]
    var_name_to_path: Dict[str, str]
    paths_by_decreasing_length: List[str]

    def __init__(self) -> None:
        self.path_to_var_name = {}
        self.var_name_to_path = {}
        self.paths_by_decreasing_length = []

    def add_rewrite(self, var_name: str, path: str) -> None:
        if var_name in self.var_name_to_path:
            if path == self.var_name_to_path[var_name]:
                # Ignore duplicate.
                return
            raise ValueError(
                f"Variable {var_name} is already mapped to path {self.var_name_to_path[var_name]} "
                f"for the purposes of rewriting log messages, cannot map it to {path}.")

        logging.info("Will shorten the path %s as ${%s} going forward", path, var_name)
        self.path_to_var_name[path] = var_name
        self.var_name_to_path[var_name] = path
        self.paths_by_decreasing_length = sorted([
            path for path in self.path_to_var_name.keys()
        ], key=lambda s: len(s), reverse=True)

    def rewrite_arg(self, arg: str) -> str:
        for path in self.paths_by_decreasing_length:
            if arg.startswith(path + '/'):
                return "${%s}/%s" % (self.path_to_var_name[path], arg[len(path) + 1:])
            if arg == path:
                return "${%s}" % self.path_to_var_name[path]
        return arg


g_log_arg_rewriter: Optional[LogArgRewriter] = None


def get_log_arg_rewriter() -> LogArgRewriter:
    global g_log_arg_rewriter
    if g_log_arg_rewriter is not None:
        return g_log_arg_rewriter
    g_log_arg_rewriter = LogArgRewriter()
    g_log_arg_rewriter.add_rewrite('YB_SRC_ROOT', YB_SRC_ROOT)
    return g_log_arg_rewriter


def rewrite_args(args: List[Any]) -> List[Any]:
    return [
        get_log_arg_rewriter().rewrite_arg(arg)
        if isinstance(arg, str) else arg
        for arg in args
    ]


def log_info(format_str: str, *args: Any) -> None:
    logging.info(format_str, *rewrite_args(cast(List[Any], args)))


def log_warning(format_str: str, *args: Any) -> None:
    logging.warning(format_str, *rewrite_args(cast(List[Any], args)))


def log_error(format_str: str, *args: Any) -> None:
    logging.error(format_str, *rewrite_args(cast(List[Any], args)))


def set_thirdparty_dir(thirdparty_dir: str) -> None:
    get_log_arg_rewriter().add_rewrite('YB_THIRDPARTY_DIR', thirdparty_dir)


def set_thirdparty_installed_dir(thirdparty_installed_dir: str) -> None:
    get_log_arg_rewriter().add_rewrite('YB_THIRDPARTY_INSTALLED_DIR', thirdparty_installed_dir)


def set_build_root(build_root: str) -> None:
    get_log_arg_rewriter().add_rewrite('YB_BUILD_ROOT', build_root)


def set_linuxbrew_dir(linuxbrew_dir: str) -> None:
    get_log_arg_rewriter().add_rewrite('YB_LINUXBREW_DIR', linuxbrew_dir)


def rewrite_logging_arg(arg: str) -> str:
    return get_log_arg_rewriter().rewrite_arg(arg)
