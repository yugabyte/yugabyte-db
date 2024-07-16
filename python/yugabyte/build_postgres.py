#!/usr/bin/env python3

# Copyright (c) YugaByte, Inc.
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
A wrapper script around PostgreSQL build that allows building debug and release versions in separate
directories.
"""

import logging
import multiprocessing
import os
import pathlib
import re
import semantic_version  # type: ignore
import shlex
import shutil
import subprocess
import sys
import time

from overrides import overrides
from sys_detection import local_sys_conf

from typing import List, Dict, Optional, Any, Set, Callable

from yugabyte_pycommon import (  # type: ignore
    run_program,
    WorkDirContext,
    quote_for_bash,
    is_verbose_mode
)

from yugabyte.tool_base import YbBuildToolBase
from yugabyte.common_util import (
    YB_SRC_ROOT,
    get_build_type_from_build_root,
    get_bool_env_var,
    get_absolute_path_aliases,
    EnvVarContext,
    shlex_join,
    check_arch,
    is_macos_arm64,
    init_logging,
)
from yugabyte.json_util import write_json_file, read_json_file
from yugabyte import compile_commands
from yugabyte.compile_commands import (
    create_compile_commands_symlink,
    get_compile_commands_file_path,
)
from yugabyte.compile_commands_processor import CompileCommandProcessor
from yugabyte.cmake_cache import CMakeCache, load_cmake_cache
from yugabyte.file_util import mkdir_p
from yugabyte.string_util import compute_sha256
from yugabyte.timestamp_saver import TimestampSaver
from yugabyte.common_util import get_target_arch


ALLOW_REMOTE_COMPILATION = True
BUILD_STEPS = (
    'configure',
    'genbki',
    'make',
)
CONFIG_ENV_VARS = [
    'CFLAGS',
    'CXXFLAGS',
    'LDFLAGS',
    'PATH',
    'YB_BUILD_ROOT',
    'YB_BUILD_TYPE',
    'YB_SRC_ROOT',
    'YB_THIRDPARTY_DIR',
    'PKG_CONFIG_PATH',
]
REMOVE_CONFIG_CACHE_MSG_RE = re.compile(r'error: run.*\brm config[.]cache\b.*and start over')
TRANSIENT_BUILD_ERRORS = ['missing separator.  Stop.']
TRANSIENT_BUILD_RETRIES = 3

COMPILER_AND_LINKER_FLAG_ENV_VAR_NAMES = ['CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'LDFLAGS_EX']

# CPPFLAGS are preprocessor flags.
ALL_FLAG_ENV_VAR_NAMES = COMPILER_AND_LINKER_FLAG_ENV_VAR_NAMES + ['CPPFLAGS']

# These files include generated files from the same directory as the including file itself, so their
# directory need to be added to the list of include directories when we generate compilation
# commands.
#
# E.g. guc.c includes guc-file.c using the line
#
# #include "guc-file.c"
#
# which works during the build because guc-file.c is located at
#
# $BUILD_ROOT/postgres_build/src/backend/utils/misc/guc-file.c
#
# and the file we are compiling is in the same directory.
#
# However, when we rewrite the compilation command to refer to refer to the guc.c file as
# src/postgres/src/backend/utils/misc/guc.c instead, there is no correspoinding guc-file.c in the
# same directory, so we have to add -I$BUILD_ROOT/postgres_build/src/backend/utils/misc to the
# command in compile_commands.json.
FILES_INCLUDING_GENERATED_FILES_FROM_SAME_DIR = ['guc.c', 'tuplesort.c']

UNDEFINED_DYNAMIC_LOOKUP_FLAG_RE = re.compile(r'\s-undefined\s+dynamic_lookup\b')


def adjust_compiler_flag(flag: str, step: str, language: str) -> Optional[str]:
    """
    Adjust a given compiler flag according to the build step and language. If this returns None,
    the flag should be removed from the command line.
    """
    assert language in ('c', 'c++')
    assert step in BUILD_STEPS
    if language == 'c' and flag in ('-Wreorder', '-Wnon-virtual-dtor'):
        # Skip C++-only flags.
        return None

    if step == 'configure':
        if flag == '-Werror':
            # Skip this flag altogether during the configure step.
            return None

    if step == 'make':
        # No changes.
        return flag

    if flag.startswith('-Werror='):
        # Convert e.g. -Werror=implicit-function-declaration to -Wimplicit-function-declaration.
        return '-W' + flag[8:]

    return flag


def adjust_linker_flag(flag: str, step: str) -> Optional[str]:
    assert step in BUILD_STEPS
    if step == 'configure':
        # During the configuration step, do not ignore unresolved symbols. This may cause the
        # configure script to conclude that certain functions are present that are actually
        # absent, e.g. fls.
        if flag in ['-Wl,--allow-shlib-undefined', '-Wl,--unresolved-symbols=ignore-all']:
            return None
    return flag


def adjust_compiler_or_linker_flags(
        flags_str: str,
        step: str,
        flag_var_name: str) -> str:
    """
    >>> adjust_compiler_or_linker_flags('-some_flag -undefined dynamic_lookup -some_other_flag',
    ...                                 'configure',
    ...                                 'LDFLAGS_EX')
    '-some_flag -some_other_flag'
    >>> adjust_compiler_or_linker_flags('-undefined dynamic_lookup -some_other_flag',
    ...                                 'configure',
    ...                                 'LDFLAGS_EX')
    '-some_other_flag'
    """
    assert flag_var_name in COMPILER_AND_LINKER_FLAG_ENV_VAR_NAMES
    assert step in BUILD_STEPS
    adjust_fn: Callable[[str], Optional[str]]
    if flag_var_name.startswith('LD'):
        def adjust_fn(flag: str) -> Optional[str]:
            return adjust_linker_flag(flag, step)
    else:
        language = 'c++' if flag_var_name.startswith('CXX') else 'c'

        def adjust_fn(flag: str) -> Optional[str]:
            return adjust_compiler_flag(flag, step, language)
    adjusted_optional_flag_list = [adjust_fn(flag) for flag in flags_str.split()]
    new_flags_str = ' '.join([
        flag for flag in adjusted_optional_flag_list if flag is not None
    ])
    if step == 'configure' and flag_var_name.startswith('LD'):
        # We remove the "-undefined dynamic_lookup" flag from the linker flags for the same reason
        # as we remove other flags that tell the linker to ignore unresolved symbols. We have to
        # do it here with a regex because it consists of two separate arguments.
        new_flags_str = ' '.join(
            UNDEFINED_DYNAMIC_LOOKUP_FLAG_RE.sub(' ', ' ' + new_flags_str).strip().split()
        )
    return new_flags_str


def remove_repeated_arguments(removing_from: str, removing_what: str) -> str:
    """
    Removes arguments from the first argument list, represented as a Bash-formatted string,
    if they are present in the second argument list. Does not change the order of arguments.
    >>> remove_repeated_arguments('--foo --bar', '--foo')
    '--bar'
    >>> remove_repeated_arguments('--baz "foo bar" --baz', "'foo bar' 'baz'")
    '--baz --baz'
    """
    args_seen: Set[str] = set()
    args_to_remove: Set[str] = set(shlex.split(removing_what))
    result: List[str] = []
    for arg in shlex.split(removing_from):
        if arg not in args_to_remove:
            result.append(arg)
    return shlex_join(result)


class PostgresBuilder(YbBuildToolBase):
    build_root: str
    pg_build_root: str
    pg_prefix: str
    build_type: str
    postgres_src_dir: str
    pg_config_path: str
    compiler_type: str
    env_vars_for_build_stamp: Set[str]
    shared_library_suffix: str
    cmake_cache: CMakeCache

    def __init__(self) -> None:
        super().__init__()
        self.env_vars_for_build_stamp = set()

        # Check if the outer build is using runs the compiler on build workers.
        self.build_uses_remote_compilation = os.environ.get('YB_REMOTE_COMPILATION') == '1'
        if self.build_uses_remote_compilation == 'auto':
            raise RuntimeError(
                "No 'auto' value is allowed for YB_REMOTE_COMPILATION at this point")

    def get_yb_version(self) -> str:
        with open(os.path.join(YB_SRC_ROOT, 'version.txt'), "r") as version_file:
            return version_file.read().strip()

    def set_env_var(self, name: str, value: Optional[str]) -> None:
        if value is None:
            if name in os.environ:
                del os.environ[name]
        else:
            os.environ[name] = value
        self.env_vars_for_build_stamp.add(name)

    def append_to_env_var(self, name: str, to_append: str, separator: str = ' ') -> None:
        if name in os.environ and os.environ[name]:
            self.set_env_var(name, os.environ[name] + separator + to_append)
        else:
            self.set_env_var(name, to_append)

    @overrides
    def add_command_line_args(self) -> None:
        parser = self.arg_parser
        parser.add_argument('--clean',
                            action='store_true',
                            help='Clean PostgreSQL build and installation directories.')

        parser.add_argument('--run_tests',
                            action='store_true',
                            help='Run PostgreSQL tests after building it.')
        parser.add_argument('--step',
                            choices=BUILD_STEPS,
                            help='Run a specific step of the build process')
        parser.add_argument(
            '--shared_library_suffix',
            help='Shared library suffix used on the current platform. Used to set DLSUFFIX '
                 'in compile_commands.json.')

    @overrides
    def validate_and_process_args(self) -> None:
        if not self.args.build_root:
            raise RuntimeError("Neither BUILD_ROOT or --build-root specified")

        self.build_root = os.path.abspath(self.args.build_root)
        self.build_root_realpath = os.path.realpath(self.build_root)

        self.cmake_cache = load_cmake_cache(self.build_root)

        self.build_type = get_build_type_from_build_root(self.build_root)
        self.pg_build_root = os.path.join(self.build_root, 'postgres_build')
        self.build_stamp_path = os.path.join(self.pg_build_root, 'build_stamp')
        self.pg_prefix = os.path.join(self.build_root, 'postgres')
        self.postgres_src_dir = os.path.join(YB_SRC_ROOT, 'src', 'postgres')
        self.pg_config_path = os.path.join(self.build_root, 'postgres', 'bin', 'pg_config')
        self.compiler_type = self.cmake_cache.get_or_raise('YB_COMPILER_TYPE')

        # A space-separated list of include directories to specify when configuring Postgres.
        self.include_dirs = self.cmake_cache.get_or_raise('PG_INCLUDE_DIRS')

        # The same but for library directories.
        self.lib_dirs = self.cmake_cache.get_or_raise('PG_LIB_DIRS')

        if not self.compiler_type:
            raise RuntimeError(
                "Compiler type not specified using either --compiler_type or YB_COMPILER_TYPE")

        self.export_compile_commands = os.environ.get('YB_EXPORT_COMPILE_COMMANDS') == '1'
        self.skip_pg_compile_commands = os.environ.get('YB_SKIP_PG_COMPILE_COMMANDS') == '1'
        self.should_configure = self.args.step is None or self.args.step == 'configure'
        self.should_genbki = self.args.step is None or self.args.step == 'genbki'
        self.should_make = self.args.step is None or self.args.step == 'make'
        self.thirdparty_dir = self.cmake_cache.get_or_raise('YB_THIRDPARTY_DIR')

        path_env_var_value: Optional[str] = os.getenv('PATH')
        if path_env_var_value is None:
            self.original_path = []
            logging.warning("PATH is not set")
        else:
            self.original_path = path_env_var_value.split(':')
            if not self.original_path:
                logging.warning("PATH is empty")

        self.compiler_family = self.cmake_cache.get_or_raise('COMPILER_FAMILY')
        self.compiler_version = self.cmake_cache.get_or_raise('COMPILER_VERSION')
        self.shared_library_suffix = self.cmake_cache.get_or_raise('YB_SHARED_LIBRARY_SUFFIX')

    def adjust_cflags_in_makefile(self) -> None:
        makefile_global_path = os.path.join(self.pg_build_root, 'src', 'Makefile.global')
        new_cflags = os.environ['CFLAGS'].strip()
        found_cflags = False

        install_cmd_line_prefix = 'INSTALL = '

        new_makefile_lines = []
        install_script_updated = False

        with open(makefile_global_path) as makefile_global_input_f:
            for line in makefile_global_input_f:
                line = line.rstrip("\n")
                if line.startswith('CFLAGS = '):
                    existing_cflags = line[9:].strip()
                    found_cflags = True
                    if existing_cflags != new_cflags:
                        line = 'CFLAGS = $(YB_PREPEND_CFLAGS) ' + \
                            new_cflags + ' $(YB_APPEND_CFLAGS)'
                        replaced_cflags = True

                if line.startswith(install_cmd_line_prefix):
                    new_makefile_lines.extend([
                        '# The following line was modified by the build_postgres.py script to use',
                        '# our install_wrapper.py script to customize installation path of',
                        '# executables and libraries, needed in case of LTO.'
                    ])
                    line = ''.join([
                        install_cmd_line_prefix,
                        os.path.join('$(YB_SRC_ROOT)', 'python', 'yugabyte', 'install_wrapper.py'),
                        ' ',
                        line[len(install_cmd_line_prefix):]
                    ])
                    install_script_updated = True

                new_makefile_lines.append(line)

        if not found_cflags:
            raise RuntimeError("Could not find a CFLAGS line in %s" % makefile_global_path)

        if not install_script_updated:
            raise RuntimeError(
                f"Could not find and update a line starting with '{install_cmd_line_prefix}' in "
                f"{makefile_global_path}.")

        if replaced_cflags:
            logging.info("Replaced cflags in %s", makefile_global_path)
            with open(makefile_global_path, 'w') as makefile_global_out_f:
                makefile_global_out_f.write("\n".join(new_makefile_lines) + "\n")

    def is_clang(self) -> bool:
        return self.compiler_family == 'clang'

    def is_gcc(self) -> bool:
        return self.compiler_family == 'gcc'

    def set_env_vars(self, step: str) -> None:
        if step not in BUILD_STEPS:
            raise RuntimeError(
                ("Invalid step specified for setting env vars: must be in {}")
                .format(BUILD_STEPS))

        is_make_step = step == 'make'

        self.set_env_var('YB_BUILD_ROOT', self.build_root)

        pkg_config_path = self.cmake_cache.get_or_raise('YB_PKG_CONFIG_PATH')
        for pkg_config_path_entry in pkg_config_path.split(':'):
            if not os.path.isdir(pkg_config_path_entry):
                raise IOError("Directory %s does not exist. PKG_CONFIG_PATH: %s" % (
                    pkg_config_path_entry, pkg_config_path))
        self.set_env_var('PKG_CONFIG_PATH', pkg_config_path)

        self.set_env_var('YB_PG_BUILD_STEP', step)
        self.set_env_var('YB_THIRDPARTY_DIR', self.thirdparty_dir)
        self.set_env_var('YB_SRC_ROOT', YB_SRC_ROOT)

        env_var_to_cmake_cache_mapping = {
            'CFLAGS': 'POSTGRES_FINAL_C_FLAGS',
            'CXXFLAGS': 'POSTGRES_FINAL_CXX_FLAGS',
            'CPPFLAGS': 'POSTGRES_EXTRA_PREPROCESSOR_FLAGS',
            'LDFLAGS': 'POSTGRES_FINAL_LD_FLAGS',

            # Extra linker flags to add after the Yugabyte libraries when linking executables. For
            # example, this can be used to add tcmalloc static library to satisfy missing symbols in
            # Yugabyte libraries. This is relevant when building with GCC and linking with ld. With
            # Clang and lld, the order of libraries does not matter.
            'YB_PG_EXE_LD_FLAGS_AFTER_YB_LIBS': 'PG_EXE_LD_FLAGS_AFTER_YB_LIBS'
        }
        for env_var_name, cmake_cache_var_name in env_var_to_cmake_cache_mapping.items():
            self.set_env_var(env_var_name, self.cmake_cache.get_or_raise(cmake_cache_var_name))

        # LDFLAGS_EX are linking arguments for executables that are used in addition to LDFLAGS.
        # Remove arguments present in LDFLAGS from LDFLAGS_EX.
        self.set_env_var(
            'LDFLAGS_EX',
            remove_repeated_arguments(
                self.cmake_cache.get_or_raise('POSTGRES_FINAL_EXE_LD_FLAGS'),
                self.cmake_cache.get_or_raise('POSTGRES_FINAL_LD_FLAGS')))

        additional_c_cxx_flags = [
            '-Wimplicit-function-declaration',
            '-Wno-error=unused-function',
            '-DHAVE__BUILTIN_CONSTANT_P=1',
            '-Werror=implicit-function-declaration',
            '-Werror=int-conversion',
        ]

        if self.is_clang():
            additional_c_cxx_flags += [
                '-Wno-builtin-requires-header',
                '-Wno-shorten-64-to-32',
            ]

        if is_make_step:
            additional_c_cxx_flags += [
                '-Wall',
                '-Werror',
                '-Wno-error=unused-function'
            ]

            if self.build_type in ['release', 'prof_gen', 'prof_use']:
                if self.is_clang():
                    additional_c_cxx_flags += [
                        '-Wno-error=array-bounds',
                        '-Wno-error=gnu-designator',
                    ]
                if self.is_gcc():
                    additional_c_cxx_flags += ['-Wno-error=strict-overflow']

            if self.build_type == 'asan':
                additional_c_cxx_flags += [
                    '-fsanitize-recover=signed-integer-overflow',
                    '-fsanitize-recover=shift-base',
                    '-fsanitize-recover=shift-exponent'
                ]

        # Tell gdb to pretend that we're compiling the code in the $YB_SRC_ROOT/src/postgres
        # directory.
        additional_c_cxx_flags += [
            '-fdebug-prefix-map=%s=%s' % (build_path, source_path)
            for build_path in get_absolute_path_aliases(self.pg_build_root)
            for source_path in get_absolute_path_aliases(self.postgres_src_dir)
        ]

        if self.is_gcc():
            additional_c_cxx_flags.append('-Wno-error=maybe-uninitialized')

        for var_name in COMPILER_AND_LINKER_FLAG_ENV_VAR_NAMES:
            os.environ[var_name] = adjust_compiler_or_linker_flags(
                    os.environ.get(var_name, '') + ' ' + ' '.join(additional_c_cxx_flags),
                    step,
                    flag_var_name=var_name)
        if step == 'make':
            self.adjust_cflags_in_makefile()

        for env_var_name in ['MAKEFLAGS']:
            if env_var_name in os.environ:
                del os.environ[env_var_name]

        compiler_wrappers_dir = os.path.join(YB_SRC_ROOT, 'build-support', 'compiler-wrappers')
        self.set_env_var('CC', os.path.join(compiler_wrappers_dir, 'cc'))
        self.set_env_var('CXX', os.path.join(compiler_wrappers_dir, 'c++'))

        self.set_env_var('LDFLAGS', re.sub(r'-Wl,--no-undefined', ' ', os.environ['LDFLAGS']))
        self.set_env_var(
            'LDFLAGS',
            re.sub(r'-Wl,--no-allow-shlib-undefined', ' ', os.environ['LDFLAGS']))

        self.append_to_env_var(
            'LDFLAGS',
            '-Wl,-rpath,' + os.path.join(self.build_root, 'lib'))

        if sys.platform != 'darwin':
            for ldflags_var_name in ['LDFLAGS', 'LDFLAGS_EX']:
                self.append_to_env_var(ldflags_var_name, '-lm')

        if is_verbose_mode():
            # CPPFLAGS are C preprocessor flags, CXXFLAGS are C++ flags.
            for env_var_name in ['CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'LDFLAGS_EX', 'LIBS']:
                if env_var_name in os.environ:
                    logging.info("%s: %s", env_var_name, os.environ[env_var_name])

        self.remote_compilation_allowed = ALLOW_REMOTE_COMPILATION and is_make_step

        self.set_env_var(
            'YB_REMOTE_COMPILATION',
            '1' if (self.remote_compilation_allowed and
                    self.build_uses_remote_compilation and
                    step == 'make') else '0'
        )

        self.set_env_var('YB_BUILD_TYPE', self.build_type)

        # Do not try to make rpaths relative during the configure step, as that may slow it down.
        self.set_env_var('YB_DISABLE_RELATIVE_RPATH', '1' if step == 'configure' else '0')

        # We need to add this directory to PATH so Postgres build could find Bison.
        thirdparty_installed_common_bin_path = os.path.join(
            self.thirdparty_dir, 'installed', 'common', 'bin')
        os.environ['PATH'] = ':'.join([thirdparty_installed_common_bin_path] + self.original_path)

        if self.build_type == 'tsan':
            self.set_env_var('TSAN_OPTIONS', os.getenv('TSAN_OPTIONS', '') + ' report_bugs=0')
            logging.info("TSAN_OPTIONS for Postgres build: %s", os.getenv('TSAN_OPTIONS'))

    def sync_postgres_source(self) -> None:
        sync_start_time_sec = time.time()
        if is_verbose_mode():
            logging.info("Syncing postgres source code")
        # Remove source code files from the build directory that have been removed from the
        # source directory.
        # TODO: extend this to the complete list of source file types.
        run_program([
            'rsync', '-avz',
            '--include', '*.c',
            '--include', '*.h',
            '--include', 'Makefile',
            '--include', '*.am',
            '--include', '*.in',
            '--include', '*.mk',
            '--exclude', '*',
            '--delete',
            self.postgres_src_dir + '/', self.pg_build_root],
            capture_output=False)
        run_program([
            'rsync',
            '-az',
            '--exclude', '*.sw?',
            '--exclude', '*.bak',
            '--exclude', '*.orig',
            '--exclude', '*.rej',
            self.postgres_src_dir + '/', self.pg_build_root],
            capture_output=False)
        sync_elapsed_time_sec = time.time() - sync_start_time_sec
        logging.info("Successfully synced postgres source code in %.2f sec",
                     sync_elapsed_time_sec)

    def clean_postgres(self) -> None:
        logging.info("Removing the postgres build and installation directories")
        for dir_to_delete in [self.pg_build_root, self.pg_prefix]:
            logging.info("Deleting the directory '%s'", dir_to_delete)
            # rm -rf is much faster than Python's rmtree.
            run_program(['rm', '-rf', dir_to_delete], capture_output=False)

    def configure_postgres(self) -> None:
        if is_verbose_mode():
            logging.info("Running configure in the postgres build directory")
        # Don't enable -Werror when running configure -- that can affect the resulting
        # configuration.
        configure_cmd_line = [
                './configure',
                '--prefix', self.pg_prefix,
                '--with-extra-version=-YB-' + self.get_yb_version(),
                '--enable-depend'
        ]
        if (not re.search(r'ubuntu2[23]\.04', local_sys_conf().short_os_name_and_version()) or
                shutil.which('msgfmt')):
            # With GCC 13 build on Ubuntu 23.04, we run into an error where Postgres configure
            # complains about not finding the msgfmt tool if we try to build Postgres with NLS.
            # This fails on our Ubuntu 23.04 x86_64 build infra Docker image but might work in a
            # different environment. Also affects clang on Ubuntu 22.04 x86_64 infra Docker image.
            #
            # TODO(mbautin): fix the root cause of this.
            configure_cmd_line.append('--enable-nls')
        configure_cmd_line += [
                '--with-icu',
                '--with-ldap',
                '--with-openssl',
                '--with-gssapi',
                # Options are ossp (original/old implementation), bsd (BSD) and e2fs
                # (libuuid-based for Unix/Mac).
                '--with-uuid=e2fs',
                '--with-libedit-preferred',
                '--with-includes=' + self.include_dirs,
                '--with-libraries=' + self.lib_dirs,
                # We're enabling debug symbols for all types of builds.
                '--enable-debug']
        if is_macos_arm64():
            configure_cmd_line.insert(0, '/opt/homebrew/bin/bash')

        if not get_bool_env_var('YB_NO_PG_CONFIG_CACHE'):
            configure_cmd_line.append('--config-cache')

        # We get readline-related errors in ASAN/TSAN, so let's disable readline there.
        if self.build_type in ['asan', 'tsan']:
            # TODO: do we still need this limitation?
            configure_cmd_line += ['--without-readline']

        if self.build_type not in ['release', 'prof_gen', 'prof_use']:
            configure_cmd_line += ['--enable-cassert']
        # Unset YB_SHOW_COMPILER_COMMAND_LINE when configuring postgres to avoid unintended side
        # effects from additional compiler output.
        with EnvVarContext(YB_SHOW_COMPILER_COMMAND_LINE=None):
            configure_result = run_program(configure_cmd_line, error_ok=True)
        if configure_result.failure():
            rerun_configure = False
            for line in configure_result.stderr.splitlines():
                if REMOVE_CONFIG_CACHE_MSG_RE.search(line.strip()):
                    logging.info("Configure failed because of stale config.cache, re-running.")
                    run_program('rm -f config.cache')
                    rerun_configure = True
                    break

            if not rerun_configure:
                logging.error("Standard error from configure:\n" + configure_result.stderr)
                config_log_path = os.path.join(self.pg_build_root, "config.log")
                if os.path.exists(config_log_path):
                    with open(config_log_path) as config_log_file:
                        config_log_str = config_log_file.read()
                    logging.info(f"Contents of {config_log_path}:")
                    sys.stderr.write(config_log_str + "\n")
                else:
                    logging.warning(f"File not found: {config_log_path}")
                raise RuntimeError("configure failed")

            configure_result = run_program(
                configure_cmd_line, shell=True, stdout_stderr_prefix='configure', error_ok=True)

        if is_verbose_mode() and configure_result.success():
            configure_result.print_output_to_stdout()

        configure_result.print_output_and_raise_error_if_failed()

        logging.info("Successfully ran configure in the postgres build directory")

    def get_env_vars_str(self, env_var_names: Set[str]) -> str:
        return "\n".join(
            "%s=%s" % (k, os.environ[k])
            for k in sorted(env_var_names)
            if k in os.environ
        )

    def get_git_version(self) -> Optional[str]:
        """Get the semantic version of git.  Assume git exists.  Return None if the version cannot
        be parsed.
        """
        version_string = subprocess.check_output(('git', '--version')).decode('utf-8').strip()
        match = re.match(r'git version (\S+)', version_string)
        assert match, f"Failed to extract git version from string: {version_string}"
        try:
            return semantic_version.Version.coerce(match.group(1))
        except ValueError as e:
            logging.warning(f"Failed to interpret git version: {e}")
            return None

    def get_build_stamp(self, include_env_vars: bool) -> str:
        """
        Creates a "build stamp" that tries to capture all inputs that might affect the PostgreSQL
        code. This is needed to avoid needlessly rebuilding PostgreSQL, as it takes ~10 seconds
        even if there are no code changes.
        """

        with WorkDirContext(YB_SRC_ROOT):
            # Postgres files.
            pathspec = [
                'src/postgres',
                'src/yb/yql/pggate',
                'python/yugabyte/build_postgres.py',
                'build-support/build_postgres',
                'CMakeLists.txt',
            ]
            git_version = self.get_git_version()
            if git_version and git_version >= semantic_version.Version('1.9.0'):
                # Git version 1.8.5 allows specifying glob pathspec, and Git version 1.9.0 allows
                # specifying negative pathspec.  Use them to exclude changes to regress test files
                # not needed for build.
                pathspec.extend([
                    ':(glob,exclude)src/postgres/**/*_schedule',
                    ':(glob,exclude)src/postgres/**/data/*.csv',
                    ':(glob,exclude)src/postgres/**/data/*.data',
                    ':(glob,exclude)src/postgres/**/expected/*.out',
                    ':(glob,exclude)src/postgres/**/input/*.source',
                    ':(glob,exclude)src/postgres/**/output/*.source',
                    ':(glob,exclude)src/postgres/**/specs/*.spec',
                    ':(glob,exclude)src/postgres/**/sql/*.sql',
                    ':(glob,exclude)src/postgres/.clang-format',
                    ':(glob,exclude)src/postgres/src/test/regress/README',
                    ':(glob,exclude)src/postgres/src/test/regress/yb_lint_regress_schedule.sh',
                ])
            # Get the most recent commit that touched postgres files.
            git_hash = subprocess.check_output(
                ['git', '--no-pager', 'log', '-n', '1', '--format=%H', '--'] + pathspec
            ).decode('utf-8').strip()
            # Get uncommitted changes to tracked postgres files.
            git_diff = subprocess.check_output(['git', 'diff', 'HEAD', '--'] + pathspec)

        env_vars_str = self.get_env_vars_str(self.env_vars_for_build_stamp)
        build_stamp = "\n".join([
            "git_commit_sha1=%s" % git_hash,
            "git_diff_sha256=%s" % compute_sha256(git_diff),
            ])

        if include_env_vars:
            build_stamp += "\nenv_vars_sha256=%s" % compute_sha256(env_vars_str)

        return build_stamp.strip()

    def get_saved_build_stamp(self) -> Optional[str]:
        if os.path.exists(self.build_stamp_path):
            with open(self.build_stamp_path) as build_stamp_file:
                return build_stamp_file.read().strip()
        return None

    def write_debug_scripts(self, env_script_content: str) -> None:
        """
        Create the following convenience scripts in the current directory:
        - env.sh: an environment setup script.
        - make.sh: a wrapper around the make command.
        """
        with open('env.sh', 'w') as out_f:
            out_f.write(env_script_content)

        make_script_path = 'make.sh'
        with open(make_script_path, 'w') as out_f:
            out_f.write(
                '\n'.join([
                    '#!/usr/bin/env bash',
                    '. "${BASH_SOURCE%/*}"/env.sh',
                    'make "$@"'
                ]) + '\n'
            )

        run_program(['chmod', 'u+x', make_script_path])

    def run_make_install(self, make_cmd: List[str], make_cmd_suffix: List[str]) -> None:
        work_dir = os.getcwd()
        complete_make_install_cmd = make_cmd + ['install'] + make_cmd_suffix
        start_time_sec = time.time()
        with TimestampSaver(self.pg_prefix, file_suffix='.h') as _:
            run_program(
                shlex_join(complete_make_install_cmd),
                stdout_stderr_prefix='make_install',
                cwd=work_dir,
                error_ok=True,
                # TODO: get rid of shell=True.
                shell=True,
            ).print_output_and_raise_error_if_failed()
            logging.info("Successfully ran 'make install' in the %s directory in %.2f sec",
                         work_dir, time.time() - start_time_sec)

    def make_postgres(self) -> None:
        self.set_env_vars('make')

        # Postgresql requires MAKELEVEL to be 0 or non-set when calling its make.
        # But in case YB project is built with make, MAKELEVEL is not 0 at this point.
        make_cmd: List[str] = ['make', 'MAKELEVEL=0']
        if is_macos_arm64():
            make_cmd = ['arch', '-arm64'] + make_cmd

        make_parallelism = self.get_make_parallelism()
        if make_parallelism:
            make_cmd += ['-j', str(make_parallelism)]

        self.set_env_var('YB_COMPILER_TYPE', self.compiler_type)

        env_script_content = self.get_env_script_content()

        pg_compile_commands_paths = []

        external_extension_dirs = [os.path.join(self.pg_build_root, d) for d
                                   in ('third-party-extensions', 'yb-extensions')]
        work_dirs = [
            self.pg_build_root,
            # self.pg_build_root,
            # os.path.join(self.pg_build_root, 'contrib'),
            # YB_TODO: begin: cleanup the below when all extensions in contrib directory work
            os.path.join(self.pg_build_root, 'contrib/adminpack'),
            os.path.join(self.pg_build_root, 'contrib/amcheck'),
            os.path.join(self.pg_build_root, 'contrib/auth_delay'),
            os.path.join(self.pg_build_root, 'contrib/auto_explain'),
            # os.path.join(self.pg_build_root, 'contrib/basebackup_to_shell'), # not in master
            # os.path.join(self.pg_build_root, 'contrib/basic_archive'), # not in master
            os.path.join(self.pg_build_root, 'contrib/bloom'),
            # os.path.join(self.pg_build_root, 'contrib/bool_plperl'), # not in master
            os.path.join(self.pg_build_root, 'contrib/btree_gin'),
            os.path.join(self.pg_build_root, 'contrib/btree_gist'),
            os.path.join(self.pg_build_root, 'contrib/citext'),
            os.path.join(self.pg_build_root, 'contrib/cube'),
            os.path.join(self.pg_build_root, 'contrib/dblink'),
            os.path.join(self.pg_build_root, 'contrib/dict_int'),
            os.path.join(self.pg_build_root, 'contrib/dict_xsyn'),
            os.path.join(self.pg_build_root, 'contrib/earthdistance'),
            os.path.join(self.pg_build_root, 'contrib/file_fdw'),
            os.path.join(self.pg_build_root, 'contrib/fuzzystrmatch'),
            os.path.join(self.pg_build_root, 'contrib/hstore'),
            # os.path.join(self.pg_build_root, 'contrib/hstore_plperl'),
            # os.path.join(self.pg_build_root, 'contrib/hstore_plpython'),
            os.path.join(self.pg_build_root, 'contrib/intagg'),
            os.path.join(self.pg_build_root, 'contrib/intarray'),
            os.path.join(self.pg_build_root, 'contrib/isn'),
            # os.path.join(self.pg_build_root, 'contrib/jsonb_plperl'),
            # os.path.join(self.pg_build_root, 'contrib/jsonb_plpython'),
            os.path.join(self.pg_build_root, 'contrib/lo'),
            os.path.join(self.pg_build_root, 'contrib/ltree'),
            # os.path.join(self.pg_build_root, 'contrib/ltree_plpython'),
            os.path.join(self.pg_build_root, 'contrib/oid2name'),
            # os.path.join(self.pg_build_root, 'contrib/old_snapshot'), # not in master
            os.path.join(self.pg_build_root, 'contrib/pageinspect'),
            os.path.join(self.pg_build_root, 'contrib/passwordcheck'),
            os.path.join(self.pg_build_root, 'contrib/pg_buffercache'),
            os.path.join(self.pg_build_root, 'contrib/pg_freespacemap'),
            os.path.join(self.pg_build_root, 'contrib/pg_prewarm'),
            os.path.join(self.pg_build_root, 'contrib/pg_stat_statements'),
            # os.path.join(self.pg_build_root, 'contrib/pg_surgery'), # not in master
            os.path.join(self.pg_build_root, 'contrib/pg_trgm'),
            os.path.join(self.pg_build_root, 'contrib/pg_visibility'),
            # os.path.join(self.pg_build_root, 'contrib/pg_walinspect'), # not in master
            os.path.join(self.pg_build_root, 'contrib/pgcrypto'),
            os.path.join(self.pg_build_root, 'contrib/pgrowlocks'),
            os.path.join(self.pg_build_root, 'contrib/pgstattuple'),
            os.path.join(self.pg_build_root, 'contrib/postgres_fdw'),
            os.path.join(self.pg_build_root, 'contrib/seg'),
            # os.path.join(self.pg_build_root, 'contrib/sepgsql'),
            os.path.join(self.pg_build_root, 'contrib/spi'),
            os.path.join(self.pg_build_root, 'contrib/sslinfo'),
            os.path.join(self.pg_build_root, 'contrib/tablefunc'),
            os.path.join(self.pg_build_root, 'contrib/tcn'),
            os.path.join(self.pg_build_root, 'contrib/test_decoding'),
            os.path.join(self.pg_build_root, 'contrib/tsm_system_rows'),
            os.path.join(self.pg_build_root, 'contrib/tsm_system_time'),
            os.path.join(self.pg_build_root, 'contrib/unaccent'),
            os.path.join(self.pg_build_root, 'contrib/uuid-ossp'),
            os.path.join(self.pg_build_root, 'contrib/vacuumlo'),
            # wal2json should be moved to third-party-extensions
            os.path.join(self.pg_build_root, 'contrib/wal2json'),
            # os.path.join(self.pg_build_root, 'contrib/xml2'),
            # YB_TODO: end
        ] + external_extension_dirs

        for work_dir in work_dirs:
            with WorkDirContext(work_dir):
                self.write_debug_scripts(env_script_content)

                make_cmd_suffix = []
                if work_dir in external_extension_dirs:
                    make_cmd_suffix = ['PG_CONFIG=' + self.pg_config_path]

                # Actually run Make.
                if is_verbose_mode():
                    logging.info("Running make in the %s directory", work_dir)

                complete_make_cmd = make_cmd + make_cmd_suffix
                complete_make_cmd_str = shlex_join(complete_make_cmd)
                self.run_make_with_retries(work_dir, complete_make_cmd_str)

                if self.build_type != 'compilecmds' or work_dir == self.pg_build_root:
                    self.run_make_install(make_cmd, make_cmd_suffix)
                else:
                    logging.info(
                            "Not running 'make install' in the %s directory since we are only "
                            "generating the compilation database", work_dir)

                if self.export_compile_commands and not self.skip_pg_compile_commands:
                    logging.info("Generating the compilation database in directory '%s'", work_dir)

                    compile_commands_path = os.path.join(work_dir, 'compile_commands.json')
                    self.set_env_var('YB_PG_SKIP_CONFIG_STATUS', '1')
                    if not os.path.exists(compile_commands_path):
                        run_program(
                            ['compiledb', 'make', '-n'] + make_cmd_suffix, capture_output=False)
                    del os.environ['YB_PG_SKIP_CONFIG_STATUS']

                    if not os.path.exists(compile_commands_path):
                        raise RuntimeError("Failed to generate compilation database at: %s" %
                                           compile_commands_path)
                    pg_compile_commands_paths.append(compile_commands_path)

        if self.export_compile_commands:
            self.write_compile_commands_files(pg_compile_commands_paths)

    def run_make_with_retries(self, work_dir: str, complete_make_cmd_str: str) -> None:
        """
        Runs Make in the current directory with up to TRANSIENT_BUILD_RETRIES retries.
        """
        attempt = 0
        start_time_sec = time.time()
        while attempt <= TRANSIENT_BUILD_RETRIES:
            attempt += 1
            attempt_start_time_sec = time.time()
            make_result = run_program(
                complete_make_cmd_str,
                stdout_stderr_prefix='make',
                cwd=work_dir,
                error_ok=True,
                shell=True  # TODO: get rid of shell=True.
            )
            if make_result.failure():
                transient_err = False
                stderr_lines = make_result.get_stderr().split('\n')
                for line in stderr_lines:
                    if any(transient_error_pattern in line
                           for transient_error_pattern in TRANSIENT_BUILD_ERRORS):
                        transient_err = True
                        logging.info(f'Transient error: {line}')
                        break
                if transient_err:
                    logging.info(
                        f"Transient error during build attempt {attempt}. "
                        f"Re-trying make command: {complete_make_cmd_str}.")
                else:
                    make_result.print_output_to_stdout()
                    raise RuntimeError("PostgreSQL compilation failed")
            else:
                attempt_elapsed_time_sec = time.time() - attempt_start_time_sec
                log_msg = "Successfully ran 'make' in the %s directory in %.2f sec"
                log_args = [work_dir, attempt_elapsed_time_sec]
                if attempt > 1:
                    log_msg += "at attempt %d, total time for all attempts: %.2f sec"
                    log_args.extend([attempt, time.time() - start_time_sec])
                logging.info(log_msg, *log_args)
                return
        else:
            raise RuntimeError(
                    f"Maximum build attempts reached ({TRANSIENT_BUILD_RETRIES} attempts).")

    def get_env_script_content(self) -> str:
        """
        Returns a Bash script that sets all variables necessary to easily rerun the "make" step
        of Postgres build (either for Postgres itself or for some contrib project).
        """
        env_script_content = ''
        for env_var_name in CONFIG_ENV_VARS:
            env_var_value = os.environ.get(env_var_name)
            if env_var_value is None:
                raise RuntimeError("Expected env var %s to be set" % env_var_name)
            env_script_content += "export %s=%s\n" % (env_var_name, quote_for_bash(env_var_value))
        return env_script_content

    def get_make_parallelism(self) -> Optional[int]:
        make_parallelism_str: Optional[str] = os.environ.get('YB_MAKE_PARALLELISM')
        make_parallelism: Optional[int] = None
        if make_parallelism_str is not None:
            make_parallelism = int(make_parallelism_str)
        if self.build_uses_remote_compilation and not self.remote_compilation_allowed:
            # Since we're building everything locally in this case, and YB_MAKE_PARALLELISM is
            # likely specified for distributed compilation, cap it at some factor times the number
            # of CPU cores.
            parallelism_cap = multiprocessing.cpu_count() * 2
            if make_parallelism:
                make_parallelism = min(parallelism_cap, make_parallelism)
            else:
                make_parallelism = parallelism_cap
        return make_parallelism

    def write_compile_commands_file(
            self,
            compile_commands: List[Dict[str, Any]],
            subdir_name: str) -> str:
        out_path = get_compile_commands_file_path(self.build_root, subdir_name)
        pathlib.Path(os.path.dirname(out_path)).mkdir(parents=True, exist_ok=True)
        write_json_file(
            json_data=compile_commands,
            output_path=out_path,
            description_for_log=f'{subdir_name} compilation commands')
        return out_path

    def write_compile_commands_files(self, pg_compile_commands_paths: List[str]) -> None:
        """
        Write various types of compilation command files across the following dimensions:
        - YugabyteDB distributed system C++ code ("YB") vs. PostgreSQL C code ("PG")
        - Raw vs. postprocessed. Postprocessed means the paths are adjusted to refer to the original
          source directory as opposed to equivalent files in the build directory, and the format is
          unified.
        """

        # Write files with raw compilation commands

        yb_compile_commands_path = os.path.join(self.build_root, 'compile_commands.json')
        yb_raw_compile_commands = read_json_file(yb_compile_commands_path)
        self.write_compile_commands_file(
            yb_raw_compile_commands, compile_commands.YB_RAW_DIR_NAME)

        pg_raw_compile_commands = []
        for compile_commands_path in pg_compile_commands_paths:
            pg_raw_compile_commands += read_json_file(compile_commands_path)

        self.write_compile_commands_file(pg_raw_compile_commands, compile_commands.PG_RAW_DIR_NAME)

        combined_raw_compile_commands = yb_raw_compile_commands + pg_raw_compile_commands
        self.write_compile_commands_file(
            combined_raw_compile_commands, compile_commands.COMBINED_RAW_DIR_NAME)

        # Write similar files with postprocessed compilation commands.
        compile_command_processor = CompileCommandProcessor(
            build_root=self.build_root,
            add_original_dir_to_path_for_files=set(FILES_INCLUDING_GENERATED_FILES_FROM_SAME_DIR),
            resolved_c_compiler=self.cmake_cache.get('YB_RESOLVED_C_COMPILER'),
            resolved_cxx_compiler=self.cmake_cache.get('YB_RESOLVED_CXX_COMPILER'))

        # -----------------------------------------------------------------------------------------
        # Non-Postgres commands
        # -----------------------------------------------------------------------------------------

        yb_postprocessed_compile_commands = [
            compile_command_processor.postprocess_compile_command(item)
            for item in yb_raw_compile_commands
        ]
        self.write_compile_commands_file(
            yb_postprocessed_compile_commands,
            compile_commands.YB_POSTPROCESSED_DIR_NAME)

        # -----------------------------------------------------------------------------------------
        # Postgres postprocessed commands
        # -----------------------------------------------------------------------------------------

        # Infer the value of the -DDLSUFFIX compiler flag from the raw Postgres compilation
        # commands. We will apply the same value of the flag to all Postgres compilation commands,
        # because some of them depend on it. This comes from the error clangd-indexer reported on
        # the following files:
        #   src/postgres/src/backend/jit/jit.c
        #   src/postgres/src/backend/utils/fmgr/dfmgr.c
        compile_command_processor.infer_preprocessor_definition('DLSUFFIX', pg_raw_compile_commands)

        # Also make sure YB_SO_MAJOR_VERSION is set consistently for all Postgres compilation
        # commands. We get multiple compilation commands for files such as
        # src/postgres/src/timezone/localtime.c and src/postgres/src/timezone/pgtz.c,
        # where the only difference between command lines is presence of absence of
        # YB_SO_MAJOR_VERSION.
        compile_command_processor.infer_preprocessor_definition(
            'YB_SO_MAJOR_VERSION', pg_raw_compile_commands)

        pg_postprocessed_compile_commands = [
            compile_command_processor.postprocess_compile_command(item)
            for item in pg_raw_compile_commands
        ]
        self.write_compile_commands_file(
            pg_postprocessed_compile_commands,
            compile_commands.PG_POSTPROCESSED_DIR_NAME)

        # -----------------------------------------------------------------------------------------
        # Combined postprocessed commands
        # -----------------------------------------------------------------------------------------

        combined_postprocessed_compile_commands = (
            yb_postprocessed_compile_commands + pg_postprocessed_compile_commands)

        combined_postprocessed_compile_commands_path = self.write_compile_commands_file(
            compile_command_processor.deduplicate_commands(combined_postprocessed_compile_commands),
            compile_commands.COMBINED_POSTPROCESSED_DIR_NAME)

        create_compile_commands_symlink(combined_postprocessed_compile_commands_path)

    @overrides
    def run_impl(self) -> None:
        self.build_postgres()

    def steps_description(self) -> str:
        if self.args.step is None:
            return "all steps in {}".format(BUILD_STEPS)
        return "the '%s' step" % (self.args.step)

    def build_postgres(self) -> None:
        start_time_sec = time.time()
        if self.args.clean:
            self.clean_postgres()

        mkdir_p(self.pg_build_root)
        if self.should_configure:
            # Regardless of build stamp, the postgres code in src should be synced to the code in
            # build.  It's fine to do this only for the configure step because the make step depends
            # on the configure step as defined in src/postgres/CMakeLists.txt.
            with WorkDirContext(self.pg_build_root):
                self.sync_postgres_source()

        self.set_env_vars('configure')
        saved_build_stamp = self.get_saved_build_stamp()
        initial_build_stamp = self.get_build_stamp(include_env_vars=True)
        initial_build_stamp_no_env = self.get_build_stamp(include_env_vars=False)
        logging.info("PostgreSQL build stamp (%s):\n%s",
                     os.path.relpath(self.build_stamp_path, YB_SRC_ROOT),
                     initial_build_stamp)

        if initial_build_stamp == saved_build_stamp:
            if self.export_compile_commands and not self.skip_pg_compile_commands:
                logging.info(
                    "Even though PostgreSQL is already up-to-date in directory %s, we still need "
                    "to create compile_commands.json, so proceeding with %s",
                    self.pg_build_root, self.steps_description())
            else:
                logging.info(
                    "PostgreSQL is already up-to-date in directory %s, skipping %s.",
                    self.pg_build_root, self.steps_description())
                return

        with WorkDirContext(self.pg_build_root):
            if self.should_configure:
                configure_start_time_sec = time.time()
                self.configure_postgres()
                logging.info("The configure step of building PostgreSQL took %.1f sec",
                             time.time() - configure_start_time_sec)
            if self.should_genbki:
                make_start_time_sec = time.time()
                self.run_make_with_retries(
                    self.pg_build_root,
                    shlex_join(['make', '-C', 'src/backend/catalog', 'bki-stamp']))
                logging.info("The genbki step of building PostgreSQL took %.1f sec",
                             time.time() - make_start_time_sec)
            if self.should_make:
                make_start_time_sec = time.time()
                self.make_postgres()
                logging.info("The make step of building PostgreSQL took %.1f sec",
                             time.time() - make_start_time_sec)

        if self.should_make:
            # Guard against the code having changed while we were building it.
            final_build_stamp_no_env = self.get_build_stamp(include_env_vars=False)
            if final_build_stamp_no_env == initial_build_stamp_no_env:
                logging.info("Updating build stamp file at %s", self.build_stamp_path)
                with open(self.build_stamp_path, 'w') as build_stamp_file:
                    build_stamp_file.write(initial_build_stamp)
            else:
                logging.warning("PostgreSQL build stamp changed during the build! Not updating.")

        logging.info(
            "PostgreSQL build (%s) took %.1f sec",
            self.steps_description(), time.time() - start_time_sec)


def main() -> None:
    init_logging(verbose=False)
    check_arch()
    if get_bool_env_var('YB_SKIP_POSTGRES_BUILD'):
        logging.info("Skipping PostgreSQL build (YB_SKIP_POSTGRES_BUILD is set)")
        return
    PostgresBuilder().run()


if __name__ == '__main__':
    main()
