#!/usr/bin/env python3

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
Simplifies a test log to make it more human-readable. Implemented as a wrapper around grep and sed
commands.
"""

import os
import sys
import re
import argparse
import subprocess
import logging
import datetime
import random
import atexit

from typing import List, Optional, Set, Any, Tuple, Dict

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from yugabyte.common_util import init_logging, shlex_join  # noqa

UUID_RE_STR = '[0-9a-f]{32}'
TABLET_OR_PEER_ID_RE_STR = r'\b[T|P] [0-9a-f]{32}\b'
RUNNING_ON_HOST_PREFIX = 'Running on host'
RUNNING_ON_HOST_RE_STR = RUNNING_ON_HOST_PREFIX + ':? [a-zA-Z0-9_.-]+$'
SYS_CATALOG_TABLET_ID = '0' * 32

# E.g. "[m-1]" or "[ts-1]" or "[P-m-1]"" prefix for C++ tests, and "m1|" or "ts1|" prefix for tests.
DAEMON_ID_PREFIX_RE_STR = '|'.join([
    r'^\[((\w-)*(m|ts))-[0-9]+\]',
    r'(m|ts)[0-9]+[|]'
])

TABLE_NAME_RE_STR = '[0-9a-zA-Z_.-]+'

PROCESSING_CREATE_TABLET_RE_STR = ''.join([
    r'Processing CreateTablet for ',
    rf'T ({UUID_RE_STR}) P ({UUID_RE_STR}) ',
    rf'[(]table=({TABLE_NAME_RE_STR}) \[id=({UUID_RE_STR})\][)], partition=(.*)$'
])

PROCESSING_CREATE_TABLET_RE = re.compile(PROCESSING_CREATE_TABLET_RE_STR)

SYS_CATALOG_TABLET_ID = '0' * 32


def normalize_daemon_id(s: str) -> str:
    '''
    >>> normalize_daemon_id('[m-1]')
    'm1'
    >>> normalize_daemon_id('m1|')
    'm1'
    >>> normalize_daemon_id('[P-m-1]')
    'Pm1'
    '''
    if s.startswith('['):
        assert s.endswith(']')
        return s[1:-1].replace('-', '')
    assert s.endswith('|')
    return s[:-1]


def pipe_to_sort_uniq(p: subprocess.Popen, count: bool) -> subprocess.Popen:
    sort_process = subprocess.Popen(['sort'], stdin=p.stdout, stdout=subprocess.PIPE)
    uniq_args = ['uniq']
    if count:
        uniq_args.append('-c')
    uniq_process = subprocess.Popen(uniq_args, stdin=sort_process.stdout, stdout=subprocess.PIPE)
    return uniq_process


def do_popen(cmd_line: List[str], stdin: Any = None) -> subprocess.Popen:
    """
    A convenient wrapper around popen.
    """
    logging.debug("Grep command line: %s", shlex_join(cmd_line))
    return subprocess.Popen(
        cmd_line,
        stdin=stdin,
        stdout=subprocess.PIPE)


def escape_for_perl(s: str) -> str:
    return s.replace(r'/', r'\/').replace(r'$', r'\$')


def get_stack_trace_replacements() -> List[Tuple[str, str]]:
    """
    Returns string replacements to use for cleaning up symbolized C++ function signatures in stack
    traces.
    """
    return [
        (
            'std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>',
            'string'
        ),
        ('std::__1::', 'std::'),
    ]


class LogRewriterConf:
    input_log_path: str
    verbose: bool
    yb_src_root: str
    build_root: str
    test_tmpdir: Optional[str]
    replace_original: bool
    output_log_path: str

    def __init__(
            self,
            input_log_path: str,
            verbose: bool,
            yb_src_root: str,
            build_root: str,
            test_tmpdir: Optional[str],
            replace_original: bool,
            output_log_path: str) -> None:
        self.input_log_path = input_log_path
        self.verbose = verbose
        self.yb_src_root = yb_src_root
        self.build_root = build_root
        self.test_tmpdir = test_tmpdir
        self.replace_original = replace_original
        self.output_log_path = output_log_path


class LogRewriter:
    conf: LogRewriterConf
    args: argparse.Namespace
    propagated_messages: List[str]

    tablet_ids: Set[str]
    peer_ids: Set[str]

    # The mapping between "daemon ids" (such as m1, m2, m3, etc. for masters, and ts1, ts2, ts3,
    # etc. for tablet servers) and peer ids.
    peer_to_daemon_id: Dict[str, str]
    daemon_to_peer_id: Dict[str, str]
    rewriting_peer_ids: bool

    table_id_to_name: Dict[str, str]
    table_id_to_tablet_ids: Dict[str, Set[str]]
    tablet_id_to_table_id_and_partition: Dict[str, Tuple[str, str]]

    # Replacements to perform in the log file.
    regex_replacements: List[Tuple[str, str]]
    test_host_name: Optional[str]

    def __init__(self, conf: LogRewriterConf) -> None:
        self.propagated_messages = []
        self.conf = conf

    def add_propagated_msg(self, fmt: str, *args: Any) -> None:
        """
        Add a message appended to the resulting log file itself.
        """
        msg: str = fmt % args
        self.propagated_messages.append(msg)

    def get_egrep_cmd_line(
            self,
            egrep_args: List[str],
            provide_input: bool = True) -> List[str]:
        """
        Compose the command line for a single "grep -E ..." command.
        :param ergrep_args: Arguments to the grep -E command
        :param provide_input: whether to provide the original log file as input
        :return: the full list of arguments to the grep command, including the grep command itself
        """
        return ['grep', '-E'] + egrep_args + (
            [self.conf.input_log_path] if provide_input else []
        )

    def execute_pipeline(self, p: subprocess.Popen) -> List[str]:
        """
        Execute a pipeline, typically consisting.
        """
        stdout, _ = p.communicate()
        p.wait()
        if p.returncode == 0:
            lines = [line.strip() for line in stdout.decode('utf-8').split('\n')]
            return [line for line in lines if line]
        if p.returncode != 1:
            self.add_propagated_msg("Unexpected exit code from a pipeline: %s", p.returncode)
        return []

    def egrep_sort_uniq(self, egrep_args: List[str], count: bool) -> List[str]:
        """
        Execute a pipeline consiting of a grep -E command on the input file, followed by sort and
        uniq commands.
        :param egrep_args: arguments to the grep -E command.
        :param count: whether to add the "-c" option to the uniq command
        :return: output lines of the pipeline, or empty output in case of an error.
        """
        egrep_process = do_popen(self.get_egrep_cmd_line(egrep_args))
        return self.execute_pipeline(pipe_to_sort_uniq(egrep_process, count=count))

    def double_egrep_sort_uniq(
            self,
            egrep_args1: List[str],
            egrep_args2: List[str],
            count: bool) -> List[str]:
        """
        Execute a pipeline consiting of two grep -E commands (the first one is on the input file,
        the second one consumes the output of the first), followed by sort and uniq commands.
        :param egrep_args1: arguments to the first grep -E command.
        :param egrep_args2: arguments to the second grep -E command.
        :param count: whether to add the "-c" option to the uniq command
        :return: output lines of the pipeline, or empty output in case of an error.
        """
        egrep_process1 = do_popen(self.get_egrep_cmd_line(egrep_args1))
        egrep_process2 = do_popen(
            self.get_egrep_cmd_line(egrep_args2, provide_input=False),
            stdin=egrep_process1.stdout)
        return self.execute_pipeline(pipe_to_sort_uniq(egrep_process2, count=count))

    def get_perl_replacement_cmd(self, a: str, b: str) -> str:
        return '/'.join(['s', escape_for_perl(a), escape_for_perl(b), 'g'])

    def find_tablet_and_peer_ids(self) -> None:
        # Find all tablet ids and peer ids (UUIDs).
        tablets_and_peers = self.egrep_sort_uniq(['-o', TABLET_OR_PEER_ID_RE_STR], count=False)

        self.tablet_ids = set()
        self.peer_ids = set()
        for tablet_or_peer in tablets_and_peers:
            if tablet_or_peer.startswith('T '):
                self.tablet_ids.add(tablet_or_peer[2:])
            elif tablet_or_peer.startswith('P '):
                self.peer_ids.add(tablet_or_peer[2:])
            else:
                self.add_propagated_msg(
                    "Unrecognized tablet id or peer id match: %s", tablet_or_peer)

    def find_daemon_peer_mapping(self) -> None:

        # We will set this to false if we decide that we can't unambigously map daemon ids and
        # peer ids both ways.
        self.rewriting_peer_ids = True

        self.peer_to_daemon_id = {}
        self.daemon_to_peer_id = {}

        for peer_id in self.peer_ids:
            # A "raw" daemon id is a daemon id simply parsed out from the log, such as "[m-1]" for
            # C++ tests or "m1|" for Java tests. We will normalize it later.
            most_frequent_raw_daemon_id = ''

            # The frequency (number of lines) corresponding to the most_frequent_raw_daemon_id for
            # this peer id.
            max_freq: int = 0

            # A simple heuristic is to look for all occurrences of "P <peer_id>" for a specific
            # peer id and count different prefixes (such as "[m-1] " for C++ tests or "m1|" for
            # Java tests) for these lines, then pick the most frequent daemon id.
            for line in self.double_egrep_sort_uniq(
                    [rf'\bP {peer_id}\b'],
                    ['-o', DAEMON_ID_PREFIX_RE_STR],
                    count=True):
                freq_str, raw_daemon_id = line.strip().split()
                freq = int(freq_str)
                if freq > max_freq:
                    max_freq = freq
                    most_frequent_raw_daemon_id = raw_daemon_id

            # We found some daemon id for this peer id.
            if most_frequent_raw_daemon_id:
                # This will convert "[m-1]" or "m1|" to simply "m1".
                daemon_id = normalize_daemon_id(most_frequent_raw_daemon_id)

                # We can simply assign this here because the outer loop is over different peer ids.
                self.peer_to_daemon_id[peer_id] = daemon_id

                # Check for collisions in the reverse mapping of daemon ids to peer ids.
                if daemon_id in self.daemon_to_peer_id:
                    # The same daemon id came up as the most frequent prefix for lines containing
                    # two different peer ids. Treat this as if we don't know the peer id to
                    # daemon id mapping at all, just to be conservative.
                    #
                    # Also we don't need to check if daemon_to_peer_id[daemon_id] is the same as
                    # peer_id, because it can't be, as we are looping over different peer ids.
                    self.add_propagated_msg(
                        f'Daemon id {daemon_id} ambiguously matched two peer ids: {peer_id} and '
                        f'{self.daemon_to_peer_id[daemon_id]}, disabling peer id rewriting.'
                    )
                    rewriting_peer_ids = False
                    break
                else:
                    self.daemon_to_peer_id[daemon_id] = peer_id

    def add_table(self, table_id: str, table_name: str) -> bool:
        """
        Adds a mapping of table id to table name. If the same table id has occurred with a
        different name, this will return False, meaning we should not assume the existence of an
        unambiguous table id to name mapping.
        """

        if table_id in self.table_id_to_name:
            if self.table_id_to_name[table_id] != table_name:
                self.add_propagated_msg(
                    f"Table id to name mapping is ambiguous: table id {table_id} has occurred both "
                    f"with name '{self.table_id_to_name[table_id]}' and '{table_name}'. Skipping "
                    "rewriting tablet ids.")
                self.rewriting_tablet_ids = False
                return False
            # The same mapping already exists, nothing to do.
            return True

        # Add new mapping.
        self.table_id_to_name[table_id] = table_name
        return True

    def add_tablet_details(self, tablet_id: str, table_id: str, partition: str) -> bool:
        table_id_and_partition = (table_id, partition)
        if tablet_id in self.tablet_id_to_table_id_and_partition:
            existing_table_id_and_partition = self.tablet_id_to_table_id_and_partition[tablet_id]
            if table_id_and_partition != existing_table_id_and_partition:
                self.add_propagated_msg(
                    f"Tablet id {tablet_id} occurs with different combinations of table id and "
                    f"partition: {existing_table_id_and_partition} and {table_id_and_partition}. "
                    "Skipping rewriting tablet ids.")
                self.rewriting_tablet_ids = False
                return False
        else:
            self.tablet_id_to_table_id_and_partition[tablet_id] = table_id_and_partition

        if table_id not in self.table_id_to_tablet_ids:
            self.table_id_to_tablet_ids[table_id] = set()
        self.table_id_to_tablet_ids[table_id].add(tablet_id)
        return True

    def find_tablet_to_table_and_partition_mapping(self) -> None:
        self.rewriting_tablet_ids = True
        processing_create_tablet_results = self.egrep_sort_uniq(
            ['-o', PROCESSING_CREATE_TABLET_RE_STR], count=False)

        self.table_id_to_name = {}
        self.table_id_to_tablet_ids = {}
        self.tablet_id_to_table_id_and_partition = {}

        for processing_create_tablet in processing_create_tablet_results:
            processing_create_tablet_match = PROCESSING_CREATE_TABLET_RE.match(
                    processing_create_tablet)
            if processing_create_tablet_match:
                tablet_id, unused_peer_id, table_name, table_id, partition = \
                    processing_create_tablet_match.groups()

                if not self.add_table(table_id, table_name):
                    break

                if not self.add_tablet_details(tablet_id, table_id, partition):
                    break

    def find_test_host_name(self) -> None:
        lines = self.egrep_sort_uniq(['-o', RUNNING_ON_HOST_RE_STR], count=False)
        host_name_candidates = set()
        for line in lines:
            line = line.strip()
            if line.startswith(RUNNING_ON_HOST_PREFIX):
                line = line[len(RUNNING_ON_HOST_PREFIX):]
            line = line.strip()
            if line.startswith(':'):
                line = line[1:]
            line = line.strip()
            if line:
                host_name_candidates.add(line)
        self.test_host_name = None
        if len(host_name_candidates) == 1:
            self.test_host_name = list(host_name_candidates)[0]

    def add_value_to_var_mapping(self, value_str: str, var_name: str) -> None:
        """
        Add a replacement of a string value with a variable name. The variable name appears in
        curly braces in the output of the log, resembling Python format() substitutions. This
        mapping is reported at the bottom of the new log. Word boundaries (\b) are required around
        the matched string.
        """
        self.add_propagated_msg(f'{var_name} = {value_str}')
        replacement = '{%s}' % var_name
        # In Perl regular expressions, \b is word boundary.
        self.regex_replacements.append((rf'\b%s\b' % value_str, replacement))

    def add_env_var_mapping(self, dir_path: str, env_var_name: str) -> None:
        """
        Add a replacement of a string value with a ${...} reference to an environment variable.
        This is typically used for environment variables of our build system, e.g. ${BUILD_ROOT},
        ${YB_SRC_ROOT}, ${TEST_TMPDIR}. No word boundaries are required around the matched string.
        """
        replacement = '${%s}' % env_var_name
        self.add_propagated_msg('%s = %s', env_var_name, dir_path)
        self.regex_replacements.append((dir_path, replacement))

    def run(self) -> None:
        log_path = self.conf.input_log_path
        if log_path.endswith('.gz'):
            raise ValueError("gzipped test logs are not supported by log rewriter: %s" % log_path)

        if not os.path.exists(log_path):
            raise IOError(f"File {log_path} does not exist")

        self.find_tablet_and_peer_ids()
        self.find_daemon_peer_mapping()
        self.find_tablet_to_table_and_partition_mapping()

        self.regex_replacements = get_stack_trace_replacements()
        self.find_test_host_name()
        if self.test_host_name:
            self.add_value_to_var_mapping(self.test_host_name, 'test_host_name')

        if self.rewriting_peer_ids:
            for peer_id, daemon_id in sorted(
                    self.peer_to_daemon_id.items(),
                    # Sort by the daemon id.
                    key=lambda t: t[1]):
                peer_id_var_name = daemon_id + '_peer_id'
                self.add_value_to_var_mapping(peer_id, peer_id_var_name)

        if self.rewriting_tablet_ids:
            # Mapping of UUID tablet ids to human-readable tablet ids.
            tablet_id_to_var_name = {}
            for table_id, tablet_ids in self.table_id_to_tablet_ids.items():
                sorted_tablet_ids = sorted(tablet_ids)
                table_name = self.table_id_to_name[table_id]
                # TODO: sort tablets by their partitions, not by UUID.
                for i, tablet_id in enumerate(sorted_tablet_ids):
                    tablet_id_to_var_name[tablet_id] = f'{table_name}_tablet_id{i + 1}'

            for tablet_id, (table_id, partition) in sorted(
                    self.tablet_id_to_table_id_and_partition.items(),
                    # Sort by table id and tablet id.
                    # TODO: change to sort by partition id for each table.
                    key=lambda t: (t[1][0], t[0])):
                self.add_value_to_var_mapping(tablet_id, tablet_id_to_var_name[tablet_id])

            # Special case for the system catalog tablet id.
            self.add_value_to_var_mapping(SYS_CATALOG_TABLET_ID, 'sys_catalog_tablet_id')

        for dir_arg_name in ['build_root', 'yb_src_root', 'test_tmpdir']:
            arg_value = getattr(self.conf, dir_arg_name)
            if arg_value:
                self.add_env_var_mapping(arg_value, dir_arg_name.upper())

        # Perl seems to be faster than sed and awk at replacing multiple regular expressions.
        perl_commands = [self.get_perl_replacement_cmd(a, b) for a, b in self.regex_replacements]
        replacement_program = ';'.join(perl_commands)

        if self.conf.replace_original:
            # When replacing the original log file, we write to a temporary file first, and then
            # rename it to the original file.
            random_str = ''.join(str(random.randint(0, 9)) for _ in range(10))
            timestamp_str = datetime.datetime.now().strftime('%Y-%m-%dT%H_%M_%S')
            effective_output_log_path = '.'.join([
                self.conf.input_log_path, timestamp_str, random_str])

            def delete_tmp_file() -> None:
                if os.path.exists(effective_output_log_path):
                    try:
                        os.remove(effective_output_log_path)
                    except OSError as ex:
                        logging.error("Error deleting file %s: %s", effective_output_log_path, ex)

            atexit.register(delete_tmp_file)
        else:
            effective_output_log_path = self.conf.output_log_path

        logging.debug("Perl regex replacement program: %s", replacement_program)
        logging.debug(
            "Performing replacements with output to file %s",
            effective_output_log_path)

        with open(effective_output_log_path, 'wb') as output_file:
            replacement_cmd = [
                'perl',
                '-p',
                '-e',
                replacement_program,
                log_path,
            ]
            replacement_process = subprocess.Popen(replacement_cmd, stdout=output_file)
            _, replacement_stderr = replacement_process.communicate()
            replacement_process.wait()

        if replacement_process.returncode == 0:
            if self.conf.replace_original:
                os.rename(effective_output_log_path, self.conf.input_log_path)
        else:
            self.add_propagated_msg(
                'Failed to replace patterns in the log with command: %s. Standard error: %s',
                shlex_join(replacement_cmd),
                replacement_stderr)

        with open(self.conf.output_log_path, 'a') as append_to_file:
            horizontal_line = '-' * 80
            append_to_file.write('\n'.join(
                [
                    '',
                    horizontal_line,
                    '',
                    'Rewrites performed by the rewrite_test_log.py script:',
                    ''
                ] + self.propagated_messages + ['', horizontal_line]
            ))


class LogRewriterTool:
    rewriter: LogRewriter

    def __init__(self) -> None:
        pass

    def parse_args(self) -> None:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument(
            '--input-log-path',
            help='Input file log path. Must not be gzipped.',
            required=True)

        parser.add_argument(
            '--verbose',
            help='Produce verbose output',
            action='store_true')

        parser.add_argument(
            '--yb-src-root',
            help='YugabyteDB source tree root')
        parser.add_argument(
            '--build-root',
            help='YugabyteDB build directory root')
        parser.add_argument(
            '--test-tmpdir',
            help='The temporary directory used to run the test')

        # This group of mutually exclusive arguments determines what output file to use.
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument(
            '--replace-original',
            help='Replace the original file in case of success. In case of failure, append '
                 'the error messages to the file.',
            action='store_true')
        group.add_argument(
            '--output-log-path',
            help='Output file log path')
        group.add_argument(
            '--output-extension',
            help='The extension for the output file. Input file extension, if present, is removed '
                 'and this extension is appended to obtain the output file name.')

        args = parser.parse_args()

        init_logging(verbose=args.verbose)

        if args.output_extension:
            input_path, input_ext = os.path.splitext(args.input_log_path)
            new_ext = args.output_extension
            if not new_ext.startswith('.'):
                new_ext = '.' + new_ext
            args.output_log_path = input_path + new_ext
            logging.info("Automatically determined output log file path: %s",
                         args.output_log_path)

        if args.replace_original:
            args.output_log_path = args.input_log_path
        else:
            if not args.output_log_path:
                raise ValueError(
                    'Output log path not specified. Please use --output-log-path, '
                    '--output-extension, or --replace-original.')

            if args.input_log_path == args.output_log_path:
                raise ValueError(
                    "--replace-original not specified but input and output paths are the same: %s" %
                    args.input_log_path)

        conf = LogRewriterConf(
            input_log_path=args.input_log_path,
            verbose=args.verbose,
            yb_src_root=args.yb_src_root,
            build_root=args.build_root,
            test_tmpdir=args.test_tmpdir,
            replace_original=args.replace_original,
            output_log_path=args.output_log_path
        )
        self.rewriter = LogRewriter(conf)

    def run(self) -> None:
        self.rewriter.run()


def main() -> None:
    rewriter_tool = LogRewriterTool()
    rewriter_tool.parse_args()
    rewriter_tool.run()


if __name__ == '__main__':
    main()
