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

from subprocess import check_call

from yugabyte_pycommon import init_logging, run_program, WorkDirContext, mkdir_p, quote_for_bash, \
        is_verbose_mode

from yb import common_util
from yb.common_util import YB_SRC_ROOT, get_build_type_from_build_root, get_bool_env_var


REMOVE_CONFIG_CACHE_MSG_RE = re.compile(r'error: run.*\brm config[.]cache\b.*and start over')


def is_error_on_warning_flag(flag):
    return flag == '-Werror' or flag.startswith('-Werror=')


def filter_compiler_flags(compiler_flags, allow_error_on_warning):
    """
    This function optionaly removes flags that turn warnings into errors.
    """
    if allow_error_on_warning:
        return compiler_flags
    return ' '.join([
        flag for flag in compiler_flags.split() if not is_error_on_warning_flag(flag)])


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
            '-Wno-error=missing-prototypes',
            '-Wno-error=unused-function',
            '-DHAVE__BUILTIN_CONSTANT_P=1',
            '-DUSE_SSE42_CRC32C=1',
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
                    allow_error_on_warning=(step == 'make'))

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
        os.environ['YB_NO_REMOTE_BUILD'] = '1'

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
                '--config-cache',
                '--enable-depend',
                # We're enabling debug symbols for all types of builds.
                '--enable-debug']

        # We get readline-related errors in ASAN/TSAN, so let's disable readline there.
        if self.build_type in ['asan', 'tsan']:
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
            make_cmd += ['-j', str(int(make_parallelism))]

        # Create a script allowing to easily run "make" from the build directory with the right
        # environment.
        make_script_content = "#!/usr/bin/env bash\n"
        for env_var_name in [
                'YB_SRC_ROOT', 'YB_BUILD_ROOT', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'PATH']:
            env_var_value = os.environ[env_var_name]
            if env_var_value is None:
                raise RuntimeError("Expected env var %s to be set" % env_var_name)
            make_script_content += "export %s=%s\n" % (env_var_name, quote_for_bash(env_var_value))
        make_script_content += 'make "$@"\n'

        for work_dir in [self.pg_build_root, os.path.join(self.pg_build_root, 'contrib')]:
            with WorkDirContext(work_dir):
                # Create a script to run Make easily with the right environment.
                make_script_path = 'make.sh'
                with open(make_script_path, 'w') as out_f:
                    out_f.write(make_script_content)
                run_program(['chmod', 'u+x', make_script_path])

                # Actually run Make.
                if is_verbose_mode():
                    logging.info("Running make in the %s directory", work_dir)
                make_result = run_program(make_cmd)
                write_program_output_to_file('make', make_result, work_dir)
                make_install_result = run_program(['make', 'install'])
                write_program_output_to_file('make_install', make_install_result, work_dir)
                logging.info("Successfully ran make in the %s directory", work_dir)

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
