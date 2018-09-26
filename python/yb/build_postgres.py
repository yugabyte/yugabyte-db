#!/usr/bin/env python2

"""
A wrapper script around PostgreSQL build that allows building debug and release versions in separate
directories.
"""

import os
import logging
import argparse
import re
import sys
import multiprocessing
import subprocess
import json

from subprocess import check_call

from yugabyte_pycommon import init_logging, run_program, WorkDirContext, mkdir_p, quote_for_bash, \
        is_verbose_mode

from yb import common_util
from yb.common_util import YB_SRC_ROOT, get_build_type_from_build_root, get_bool_env_var


REMOVE_CONFIG_CACHE_MSG_RE = re.compile(r'error: run.*\brm config[.]cache\b.*and start over')

ALLOW_REMOTE_COMPILATION = False


def adjust_error_on_warning_flag(flag, step, language):
    """
    Adjust a given compiler flag according to whether this is for configure or make.
    """
    assert language in ('c', 'c++')
    assert step in ('configure', 'make')
    if language == 'c' and flag in ('-Wreorder', '-Wnon-virtual-dtor'):
        # Skip C++-only flags.
        return None

    if flag == '-Werror' and step == 'configure':
        # Skip this flag altogether during the configure step.
        return None

    if step == 'make':
        # No changes.
        return flag

    if flag.startswith('-Werror='):
        # Convert e.g. -Werror=implicit-function-declaration to -Wimplicit-function-declaration.
        return '-W' + flag[8:]

    return flag


def filter_compiler_flags(compiler_flags, step, language):
    """
    This function optionaly removes flags that turn warnings into errors.
    """
    assert language in ('c', 'c++')
    assert step in ('configure', 'make')
    adjusted_flags = [
        adjust_error_on_warning_flag(flag, step, language)
        for flag in compiler_flags.split()
    ]
    return ' '.join([
        flag for flag in adjusted_flags if flag is not None
    ])


def get_path_variants(path):
    """
    Returns a list of different variants (just an absolute path vs. all symlinks resolved) for the
    given path.
    """
    return sorted(set([os.path.abspath(path), os.path.realpath(path)]))


def write_program_output_to_file(program_name, program_result, target_dir):
    for output_type in ['out', 'err']:
        output_path = os.path.join(target_dir, '%s.%s' % (program_name, output_type))
        output_content = getattr(program_result, 'std' + output_type)
        with open(output_path, 'w') as out_f:
            out_f.write(output_content)
        if output_content.strip():
            logging.info("Wrote std%s of %s to %s", output_type, program_name, output_path)


class PostgresBuilder:
    def __init__(self):
        self.args = None
        self.build_root = None
        self.pg_build_root = None
        self.pg_prefix = None
        self.build_type = None
        self.postgres_src_dir = None
        self.compiler_type = None

        # Check if the outer build is using runs the compiler on build workers.
        self.build_uses_remote_compilation = os.environ.get('YB_REMOTE_COMPILATION')
        if self.build_uses_remote_compilation == 'auto':
            raise RuntimeError(
                "No 'auto' value is allowed for YB_REMOTE_COMPILATION at this point")

    def parse_args(self):
        parser = argparse.ArgumentParser(
            description='A tool for building the PostgreSQL code subtree in YugaByte DB codebase')
        parser.add_argument('--build_root',
                            default=os.environ.get('BUILD_ROOT'),
                            help='YugaByte build root directory. The PostgreSQL build/install '
                                 'directories will be created under here.')

        parser.add_argument('--run_tests',
                            action='store_true',
                            help='Run PostgreSQL tests after building it.')

        parser.add_argument('--clean',
                            action='store_true',
                            help='Clean PostgreSQL build and installation directories.')

        parser.add_argument('--cflags', help='C compiler flags')
        parser.add_argument('--cxxflags', help='C++ compiler flags')
        parser.add_argument('--ldflags', help='Linker flags for all binaries')
        parser.add_argument('--ldflags_ex', help='Linker flags for executables')
        parser.add_argument('--compiler_type', help='Compiler type, e.g. gcc or clang')

        self.args = parser.parse_args()
        if not self.args.build_root:
            raise RuntimeError("Neither BUILD_ROOT or --build-root specified")

        self.build_root = os.path.abspath(self.args.build_root)
        self.build_type = get_build_type_from_build_root(self.build_root)
        self.pg_build_root = os.path.join(self.build_root, 'postgres_build')
        self.pg_prefix = os.path.join(self.build_root, 'postgres')
        self.build_type = get_build_type_from_build_root(self.build_root)
        self.postgres_src_dir = os.path.join(YB_SRC_ROOT, 'src', 'postgres')
        self.compiler_type = self.args.compiler_type or os.getenv('YB_COMPILER_TYPE')
        if not self.compiler_type:
            raise RuntimeError(
                "Compiler type not specified using either --compiler_type or YB_COMPILER_TYPE")

        self.export_compile_commands = os.environ.get('YB_EXPORT_COMPILE_COMMANDS') == '1'
        # This allows to skip the time-consuming compile commands file generation if it already
        # exists during debugging of this script.
        self.export_compile_commands_lazily = \
            os.environ.get('YB_EXPORT_COMPILE_COMMANDS_LAZILY') == '1'

    def adjust_cflags_in_makefile(self):
        makefile_global_path = os.path.join(self.pg_build_root, 'src/Makefile.global')
        new_makefile_lines = []
        new_cflags = os.environ['CFLAGS'].strip()
        found_cflags = False
        repalced_cflags = False
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
                new_makefile_lines.append(line)

        if not found_cflags:
            raise RuntimeError("Could not find a CFLAGS line in %s" % makefile_global_path)

        if replaced_cflags:
            logging.info("Replaced cflags in %s", makefile_global_path)
            with open(makefile_global_path, 'w') as makefile_global_out_f:
                makefile_global_out_f.write("\n".join(new_makefile_lines) + "\n")

    def set_env_vars(self, step):
        if step not in ['configure', 'make']:
            raise RuntimeError(
                    ("Invalid step specified for setting env vars, must be either 'configure' "
                     "or 'make'").format(step))

        os.environ['YB_BUILD_ROOT'] = self.build_root
        os.environ['YB_SRC_ROOT'] = YB_SRC_ROOT

        for var_name in ['CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'LDFLAGS_EX']:
            arg_value = getattr(self.args, var_name.lower())
            if arg_value is not None:
                os.environ[var_name] = arg_value
            else:
                del os.environ[var_name]

        additional_c_cxx_flags = [
            '-Wimplicit-function-declaration',
            '-Wno-error=unused-function',
            '-DHAVE__BUILTIN_CONSTANT_P=1',
            '-DUSE_SSE42_CRC32C=1',
            '-std=c11',
            '-Werror=implicit-function-declaration',
            '-Werror=int-conversion',
        ]

        if step == 'make':
            additional_c_cxx_flags += [
                '-Wall',
                '-Werror',
                '-Wno-error=unused-function'
            ]

            if self.compiler_type == 'clang':
                additional_c_cxx_flags += [
                    '-Wno-error=builtin-requires-header'
                ]
            if self.build_type == 'release':
                if self.compiler_type == 'clang':
                    additional_c_cxx_flags += [
                        '-Wno-error=array-bounds',
                        '-Wno-error=gnu-designator'
                    ]
                if self.compiler_type == 'gcc':
                    additional_c_cxx_flags += ['-Wno-error=strict-overflow']
            if self.build_type == 'asan':
                additional_c_cxx_flags += [
                    '-fsanitize-recover=signed-integer-overflow',
                    '-fsanitize-recover=shift-base'
                ]

        # Tell gdb to pretend that we're compiling the code in the $YB_SRC_ROOT/src/postgres
        # directory.
        build_path_variants = get_path_variants(self.pg_build_root)
        src_path_variants = get_path_variants(self.postgres_src_dir)
        additional_c_cxx_flags += [
            '-fdebug-prefix-map=%s=%s' % (build_path, source_path)
            for build_path in build_path_variants
            for source_path in src_path_variants
        ]

        if self.compiler_type == 'gcc':
            additional_c_cxx_flags.append('-Wno-error=maybe-uninitialized')

        for var_name in ['CFLAGS', 'CXXFLAGS']:
            os.environ[var_name] = filter_compiler_flags(
                    os.environ.get(var_name, '') + ' ' + ' '.join(additional_c_cxx_flags),
                    step,
                    language='c' if var_name == 'CFLAGS' else 'c++')
        if step == 'make':
            self.adjust_cflags_in_makefile()

        for env_var_name in ['MAKEFLAGS']:
            if env_var_name in os.environ:
                del os.environ[env_var_name]

        compiler_wrappers_dir = os.path.join(YB_SRC_ROOT, 'build-support', 'compiler-wrappers')
        os.environ['CC'] = os.path.join(compiler_wrappers_dir, 'cc')
        os.environ['CXX'] = os.path.join(compiler_wrappers_dir, 'c++')

        os.environ['LDFLAGS'] = re.sub(r'-Wl,--no-undefined', ' ', os.environ['LDFLAGS'])
        os.environ['LDFLAGS'] = re.sub(r'-Wl,--no-allow-shlib-undefined', ' ',
                                       os.environ['LDFLAGS'])

        os.environ['LDFLAGS'] += ' -Wl,-rpath,' + os.path.join(self.build_root, 'lib')

        for ldflags_var_name in ['LDFLAGS', 'LDFLAGS_EX']:
            os.environ[ldflags_var_name] += ' -lm'

        if is_verbose_mode():
            # CPPFLAGS are C preprocessor flags, CXXFLAGS are C++ flags.
            for env_var_name in ['CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'LDFLAGS_EX', 'LIBS']:
                if env_var_name in os.environ:
                    logging.info("%s: %s", env_var_name, os.environ[env_var_name])
        # PostgreSQL builds pretty fast, and we don't want to use our remote compilation over SSH
        # for it as it might have issues with parallelism.
        self.remote_compilation_allowed = ALLOW_REMOTE_COMPILATION and step == 'make'

        os.environ['YB_REMOTE_COMPILATION'] = (
            '1' if self.remote_compilation_allowed and self.build_uses_remote_compilation
            else '0'
        )

        os.environ['YB_BUILD_TYPE'] = self.build_type

        # Do not try to make rpaths relative during the configure step, as that may slow it down.
        os.environ['YB_DISABLE_RELATIVE_RPATH'] = '1' if step == 'configure' else '0'

    def sync_postgres_source(self):
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
        if is_verbose_mode():
            logging.info("Successfully synced postgres source code")

    def clean_postgres(self):
        logging.info("Removing the postgres build and installation directories")
        for dir_to_delete in [self.pg_build_root, self.pg_prefix]:
            logging.info("Deleting the directory '%s'", dir_to_delete)
            # rm -rf is much faster than Python's rmtree.
            run_program(['rm', '-rf', dir_to_delete], capture_output=False)

    def configure_postgres(self):
        if is_verbose_mode():
            logging.info("Running configure in the postgres build directory")
        # Don't enable -Werror when running configure -- that can affect the resulting
        # configuration.
        self.set_env_vars('configure')
        configure_cmd_line = [
                './configure',
                '--prefix', self.pg_prefix,
                '--enable-depend',
                # We're enabling debug symbols for all types of builds.
                '--enable-debug']
        if not get_bool_env_var('YB_NO_PG_CONFIG_CACHE'):
            configure_cmd_line.append('--config-cache')

        # We get readline-related errors in ASAN/TSAN, so let's disable readline there.
        if self.build_type in ['asan', 'tsan']:
            # TODO: do we still need this limitation?
            configure_cmd_line += ['--without-readline']

        if self.build_type != 'release':
            configure_cmd_line += ['--enable-cassert']
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
                raise RuntimeError("configure failed")

            configure_result = run_program(configure_cmd_line)

        if is_verbose_mode():
            configure_result.print_output_to_stdout()
        write_program_output_to_file('configure', configure_result, self.pg_build_root)

        logging.info("Successfully ran configure in the postgres build directory")

    def make_postgres(self):
        self.set_env_vars('make')
        make_cmd = ['make']

        make_parallelism = os.environ.get('YB_MAKE_PARALLELISM')
        if make_parallelism:
            make_parallelism = int(make_parallelism)
        if self.build_uses_remote_compilation and not self.remote_compilation_allowed:
            # Since we're building everything locally in this case, and YB_MAKE_PARALLELISM is
            # likely specified for distributed compilation, cap it at some factor times the number
            # of CPU cores.
            parallelism_cap = multiprocessing.cpu_count() * 2
            if make_parallelism:
                make_parallelism = min(parallelism_cap, make_parallelism)
            else:
                make_parallelism = cpu_count

        if make_parallelism:
            make_cmd += ['-j', str(int(make_parallelism))]

        os.environ['YB_COMPILER_TYPE'] = self.compiler_type

        # Create a script allowing to easily run "make" from the build directory with the right
        # environment.
        env_script_content = ''
        for env_var_name in [
                'YB_SRC_ROOT', 'YB_BUILD_ROOT', 'YB_BUILD_TYPE', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS',
                'PATH']:
            env_var_value = os.environ[env_var_name]
            if env_var_value is None:
                raise RuntimeError("Expected env var %s to be set" % env_var_name)
            env_script_content += "export %s=%s\n" % (env_var_name, quote_for_bash(env_var_value))

        compile_commands_files = []

        for work_dir in [self.pg_build_root, os.path.join(self.pg_build_root, 'contrib')]:
            with WorkDirContext(work_dir):
                # Create a script to run Make easily with the right environment.
                make_script_path = 'make.sh'
                with open(make_script_path, 'w') as out_f:
                    out_f.write(
                        '#!/usr/bin/env bash\n'
                        '. "${BASH_SOURCE%/*}"/env.sh\n'
                        'make "$@"\n')
                with open('env.sh', 'w') as out_f:
                    out_f.write(env_script_content)

                run_program(['chmod', 'u+x', make_script_path])

                # Actually run Make.
                if is_verbose_mode():
                    logging.info("Running make in the %s directory", work_dir)
                make_result = run_program(make_cmd)
                write_program_output_to_file('make', make_result, work_dir)
                make_install_result = run_program(['make', 'install'])
                write_program_output_to_file('make_install', make_install_result, work_dir)
                logging.info("Successfully ran make in the %s directory", work_dir)

                if self.export_compile_commands:
                    logging.info("Generating the compilation database in directory '%s'", work_dir)

                    compile_commands_path = os.path.join(work_dir, 'compile_commands.json')
                    os.environ['YB_PG_SKIP_CONFIG_STATUS'] = '1'
                    if (not os.path.exists(compile_commands_path) or
                            not self.export_compile_commands_lazily):
                        run_program(['compiledb', 'make', '-n'], capture_output=False)
                    del os.environ['YB_PG_SKIP_CONFIG_STATUS']

                    if not os.path.exists(compile_commands_path):
                        raise RuntimeError("Failed to generate compilation database at: %s" %
                                           compile_commands_path)
                    compile_commands_files.append(compile_commands_path)

        if self.export_compile_commands:
            self.combine_compile_commands(compile_commands_files)

    def combine_compile_commands(self, compile_commands_files):
        """
        Combine compilation commands files from main and contrib subtrees of PostgreSQL, patch them
        so that they point to the original source directory, and concatenate with the main
        compilation commands file.
        """
        new_compile_commands = []
        for compile_commands_path in compile_commands_files:
            with open(compile_commands_path) as compile_commands_file:
                new_compile_commands += json.load(compile_commands_file)

        new_compile_commands = [
            self.postprocess_pg_compile_command(item)
            for item in new_compile_commands
        ]

        # Add the top-level compile commands file without any changes.
        with open(os.path.join(self.build_root, 'compile_commands.json')) as compile_commands_file:
            new_compile_commands += json.load(compile_commands_file)

        output_path = os.path.join(self.build_root, 'combined_compile_commands.json')
        with open(output_path, 'w') as compile_commands_output_file:
            json.dump(new_compile_commands, compile_commands_output_file, indent=2)
        logging.info("Wrote the compilation commands file to: %s", output_path)

    def postprocess_pg_compile_command(self, compile_command_item):
        directory = compile_command_item['directory']
        command = compile_command_item['command']
        file_path = compile_command_item['file']

        if directory.startswith(self.pg_build_root + '/'):
            new_directory = os.path.join(YB_SRC_ROOT, 'src', 'postgres',
                                         os.path.relpath(directory, self.pg_build_root))
            # Some files only exist in the postgres build directory. We don't switch the work
            # directory of the compiler to the original source directory in those cases.
            if (not os.path.isabs(file_path) and
                    not os.path.isfile(os.path.join(new_directory, file_path))):
                new_directory = directory

        new_args = []
        for arg in command.split():
            added = False
            if arg.startswith('-I'):
                include_path = arg[2:]
                if not os.path.isabs(include_path):
                    new_include_path = os.path.join(new_directory, include_path)
                    if os.path.isdir(new_include_path):
                        new_args.append('-I' + new_include_path)
                        added = True

            if not added:
                new_args.append(arg)
        new_command = ' '.join(new_args)

        if not os.path.isabs(file_path):
            new_file_path = os.path.join(new_directory, file_path)
            if not os.path.isfile(new_file_path):
                new_file_path = file_path

        return {
            'directory': new_directory,
            'command': new_command,
            'file': new_file_path
        }

    def run(self):
        if get_bool_env_var('YB_SKIP_POSTGRES_BUILD'):
            logging.info("Skipping PostgreSQL build (YB_SKIP_POSTGRES_BUILD is set)")
            return

        self.parse_args()
        self.build_postgres()

    def build_postgres(self):
        if self.args.clean:
            self.clean_postgres()

        mkdir_p(self.pg_build_root)

        with WorkDirContext(self.pg_build_root):
            self.sync_postgres_source()
            self.configure_postgres()
            self.make_postgres()


if __name__ == '__main__':
    init_logging()
    PostgresBuilder().run()
