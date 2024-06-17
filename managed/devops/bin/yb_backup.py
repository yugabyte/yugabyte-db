#!/usr/bin/env python
#
# Copyright 2022 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from __future__ import print_function

import argparse
import atexit
import copy
import logging
import pipes
import random
import string
import subprocess
import traceback
import time
import json
import signal
import sys

from argparse import RawDescriptionHelpFormatter
from datetime import datetime, timedelta
from multiprocessing.pool import ThreadPool
from contextlib import contextmanager

import os
import re
import threading

TABLET_UUID_LEN = 32
UUID_RE_STR = '[0-9a-f-]{32,36}'
COLOCATED_UUID_SUFFIX = '.colocated.parent.uuid'
COLOCATED_NAME_SUFFIX = '.colocated.parent.tablename'
COLOCATED_UUID_RE_STR = UUID_RE_STR + COLOCATED_UUID_SUFFIX
UUID_ONLY_RE = re.compile('^' + UUID_RE_STR + '$')
NEW_OLD_UUID_RE = re.compile(UUID_RE_STR + '[ ]*\t' + UUID_RE_STR)
COLOCATED_DB_PARENT_TABLE_NEW_OLD_UUID_RE = re.compile(
    COLOCATED_UUID_RE_STR + '[ ]*\t' + COLOCATED_UUID_RE_STR)
TABLEGROUP_UUID_SUFFIX = '.tablegroup.parent.uuid'
TABLEGROUP_NAME_SUFFIX = '.tablegroup.parent.tablename'
TABLEGROUP_UUID_RE_STR = UUID_RE_STR + TABLEGROUP_UUID_SUFFIX
TABLEGROUP_PARENT_TABLE_NEW_OLD_UUID_RE = re.compile(
    TABLEGROUP_UUID_RE_STR + '[ ]*\t' + TABLEGROUP_UUID_RE_STR)
COLOCATION_UUID_SUFFIX = '.colocation.parent.uuid'
COLOCATION_NAME_SUFFIX = '.colocation.parent.tablename'
COLOCATION_UUID_RE_STR = UUID_RE_STR + COLOCATION_UUID_SUFFIX
COLOCATION_PARENT_TABLE_NEW_OLD_UUID_RE = re.compile(
    COLOCATION_UUID_RE_STR + '[ ]*\t' + COLOCATION_UUID_RE_STR)
COLOCATION_MIGRATION_PARENT_TABLE_NEW_OLD_UUID_RE = re.compile(
    COLOCATED_UUID_RE_STR + '[ ]*\t' + COLOCATION_UUID_RE_STR)
LEADING_UUID_RE = re.compile('^(' + UUID_RE_STR + r')\b')

LIST_TABLET_SERVERS_RE = re.compile('.*list_tablet_servers.*(' + UUID_RE_STR + ').*')

IMPORTED_TABLE_RE = re.compile(r'(?:Colocated t|T)able being imported: ([^\.]*)\.(.*)')
RESTORATION_RE = re.compile('^Restoration id: (' + UUID_RE_STR + r')\b')

ACCESS_TOKEN_RE = re.compile(r'(.*?)--access_token=.*?( |\')')
ACCESS_TOKEN_REDACT_RE = r'\g<1>--access_token=REDACT\g<2>'

STARTED_SNAPSHOT_CREATION_RE = re.compile(r'[\S\s]*Started snapshot creation: (?P<uuid>.*)')
YSQL_CATALOG_VERSION_RE = re.compile(r'[\S\s]*Version: (?P<version>.*)')

YSQL_SHELL_ERROR_RE = re.compile('.*ERROR.*')
# This temporary variable enables the features:
# 1. "STOP_ON_ERROR" mode in 'ysqlsh' tool
# 2. New API: 'backup_tablespaces'/'restore_tablespaces'+'use_tablespaces'
#    instead of old 'use_tablespaces'
# 3. If the new API 'backup_roles' is NOT used - the YSQL Dump is generated
#    with '--no-privileges' flag.
ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR = False

ROCKSDB_PATH_PREFIX = '/yb-data/tserver/data/rocksdb'

SNAPSHOT_DIR_GLOB = '*/table-*/tablet-*.snapshots/*'
SNAPSHOT_DIR_SUFFIX_RE = re.compile(
    '^.*/tablet-({})[.]snapshots/({})$'.format(UUID_RE_STR, UUID_RE_STR))

TABLE_PATH_PREFIX_TEMPLATE = '*/table-{}'

TABLET_MASK = 'tablet-????????????????????????????????'
TABLET_DIR_GLOB = TABLE_PATH_PREFIX_TEMPLATE + '/' + TABLET_MASK

MANIFEST_FILE_NAME = 'Manifest'
METADATA_FILE_NAME = 'SnapshotInfoPB'
SQL_TBSP_DUMP_FILE_NAME = 'YSQLDump_tablespaces'
SQL_ROLES_DUMP_FILE_NAME = 'YSQLDump_roles'
SQL_DUMP_FILE_NAME = 'YSQLDump'
SQL_DATA_DUMP_FILE_NAME = 'YSQLDump_data'
CREATE_METAFILES_MAX_RETRIES = 10
CLOUD_CFG_FILE_NAME = 'cloud_cfg'
CLOUD_CMD_MAX_RETRIES = 10
LOCAL_FILE_MAX_RETRIES = 3
RESTORE_DOWNLOAD_LOOP_MAX_RETRIES = 20
REPLICAS_SEARCHING_LOOP_MAX_RETRIES = 30
SLEEP_IN_REPLICAS_SEARCHING_ROUND_SEC = 20  # 30*20 sec = 10 minutes
LEADERS_SEARCHING_LOOP_MAX_RETRIES = 5
SLEEP_IN_LEADERS_SEARCHING_ROUND_SEC = 20  # 5*(100 + 20) sec = 10 minutes
LIVE_TS_SEARCHING_LOOP_MAX_RETRIES = 40
SLEEP_IN_LIVE_TS_SEARCHING_ROUND_SEC = 15  # 40*15 sec = 10 minutes

CREATE_SNAPSHOT_TIMEOUT_SEC = 60 * 60  # hour
RESTORE_SNAPSHOT_TIMEOUT_SEC = 24 * 60 * 60  # day
# Try to read home dir from environment variable, else assume it's /home/yugabyte.
YB_HOME_DIR = os.environ.get("YB_HOME_DIR", "/home/yugabyte")
DEFAULT_REMOTE_YB_ADMIN_PATH = os.path.join(YB_HOME_DIR, 'master/bin/yb-admin')
DEFAULT_REMOTE_YSQL_DUMP_PATH = os.path.join(YB_HOME_DIR, 'master/postgres/bin/ysql_dump')
DEFAULT_REMOTE_YSQL_SHELL_PATH = os.path.join(YB_HOME_DIR, 'master/bin/ysqlsh')
DEFAULT_YB_USER = 'yugabyte'
SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
PLATFORM_VERSION_FILE_PATH = os.path.join(SCRIPT_DIR, '../../yugaware/conf/version_metadata.json')
YB_VERSION_RE = re.compile(r'^version (\d+\.\d+\.\d+\.\d+).*')
YB_ADMIN_HELP_RE = re.compile(r'^ \d+\. (\w+).*')

XXH64HASH_TOOL_PATH = os.path.join(YB_HOME_DIR, 'bin/xxhash')
XXH64HASH_TOOL_PATH_K8S = '/tmp/xxhash'
XXH64_FILE_EXT = 'xxh64'
XXH64_X86_BIN = 'xxhsum_x86'
XXH64_AARCH_BIN = 'xxhsum_aarch'
SHA_TOOL_PATH = '/usr/bin/sha256sum'
SHA_FILE_EXT = 'sha256'

DISABLE_SPLITTING_MS = 30000
DISABLE_SPLITTING_FREQ_SEC = 10
IS_SPLITTING_DISABLED_MAX_RETRIES = 100
TEST_SLEEP_AFTER_FIND_SNAPSHOT_DIRS_SEC = 100

DEFAULT_TS_WEB_PORT = 9000


@contextmanager
def terminating(thing):
    try:
        yield thing
    finally:
        thing.terminate()


class BackupException(Exception):
    """A YugaByte backup exception."""
    pass


class CompatibilityException(BackupException):
    """Exception which can be ignored for compatibility."""
    pass


class YbAdminOpNotSupportedException(BackupException):
    """Exception raised if the attempted operation is not supported by the version of yb-admin we
    are using."""
    pass


class YsqlDumpApplyFailedException(BackupException):
    """Exception raised if 'ysqlsh' tool failed to apply the YSQL dump file."""
    pass


def split_by_tab(line):
    return [item.replace(' ', '') for item in line.split("\t")]


def split_by_space(line):
    items = []
    for item in line.split(" "):
        item = item.strip()
        if item:
            items.append(item)
    return items


def quote_cmd_line_for_bash(cmd_line):
    if not isinstance(cmd_line, list) and not isinstance(cmd_line, tuple):
        raise BackupException("Expected a list/tuple, got: [[ {} ]]".format(cmd_line))
    return ' '.join([pipes.quote(str(arg)) for arg in cmd_line])


class BackupTimer:
    def __init__(self):
        # Store the start time as phase 0.
        self.logged_times = [time.time()]
        self.phases = ["START"]
        self.num_phases = 0
        self.finished = False

    def log_new_phase(self, msg="", last_phase=False):
        self.logged_times.append(time.time())
        self.phases.append(msg)
        self.num_phases += 1
        # Print completed time of last stage.
        time_taken = self.logged_times[self.num_phases] - self.logged_times[self.num_phases - 1]
        logging.info("Completed phase {}: {} [Time taken for phase: {}]".format(
            self.num_phases - 1,
            self.phases[self.num_phases - 1],
            str(timedelta(seconds=time_taken))))
        if last_phase:
            self.finished = True
        else:
            logging.info("[app] Starting phase {}: {}".format(self.num_phases, msg))

    def print_summary(self):
        if not self.finished:
            self.log_new_phase(last_phase=True)

        log_str = "Summary of run:\n"
        # Print info for each phase (ignore phase-0: START).
        for i in range(1, self.num_phases):
            t = self.logged_times[i + 1] - self.logged_times[i]
            log_str += "{} : PHASE {} : {}\n".format(str(timedelta(seconds=t)), i, self.phases[i])
        # Also print info for total runtime.
        log_str += "Total runtime: {}".format(
            str(timedelta(seconds=self.logged_times[-1] - self.logged_times[0])))
        # Add [app] for YW platform filter.
        logging.info("[app] " + log_str)

    def start_time_str(self):
        return time_to_str(self.logged_times[0])

    def end_time_str(self):
        return time_to_str(self.logged_times[self.num_phases])


class SingleArgParallelCmd:
    """
    Invokes a single-argument function on the given set of argument values in a parallel way
    using the given thread pool. Arguments are first deduplicated, so they have to be hashable.
    Example:
        SingleArgParallelCmd(fn, [a, b, c]).run(pool)
        -> run in parallel Thread-1: -> fn(a)
                           Thread-2: -> fn(b)
                           Thread-3: -> fn(c)
    """

    def __init__(self, fn, args, verbose):
        self.fn = fn
        self.args = args
        self.verbose = verbose

    def run(self, pool):
        fn_args = sorted(set(self.args))
        if self.verbose:
            utc_time = datetime.utcfromtimestamp(time.time())
            logging.info(f'Method {self.fn.__name__} with args {fn_args} queued at {utc_time}')
        return self._run_internal(self.fn, fn_args, fn_args, pool)

    def _run_internal(self, internal_fn, internal_fn_srgs, fn_args, pool):
        values = pool.map(internal_fn, internal_fn_srgs)
        # Return a map from args to the command results.
        assert len(fn_args) == len(values)
        return dict(zip(fn_args, values))


class MultiArgParallelCmd(SingleArgParallelCmd):
    """
    Invokes a function that is allowed to have any number of arguments on the given
    set of tuples of arguments in a parallel way using the given thread pool.
    Arguments are first deduplicated, so they have to be hashable.
    Example:
        MultiArgParallelCmd p(fn)
        p.add_args(a1, a2)
        p.add_args(b1, b2)
        p.run(pool)
        -> run in parallel Thread-1: -> fn(a1, a2)
                           Thread-2: -> fn(b1, b2)
    """

    def __init__(self, fn, verbose):
        self.fn = fn
        self.args = []
        self.verbose = verbose

    def add_args(self, *args_tuple):
        assert isinstance(args_tuple, tuple)
        self.args.append(args_tuple)

    def run(self, pool):
        def internal_fn(args_tuple):
            # One tuple - one function run.
            if self.verbose:
                utc_time = datetime.utcfromtimestamp(time.time())
                logging.info(f'Method {self.fn.__name__} with args {args_tuple} \
                             queued at {utc_time}')
            return self.fn(*args_tuple)

        fn_args = sorted(set(self.args))
        return self._run_internal(internal_fn, fn_args, fn_args, pool)


class SequencedParallelCmd(SingleArgParallelCmd):
    """
    Invokes commands in a parallel way using the given thread pool.
    Each command is a sequence of function calls with the provided arguments.
    Example:
        SequencedParallelCmd p(fn)
        p.start_command()  # Start sequence-1 of the function calls.
        p.add_args(a1, a2)
        p.add_args(b1, b2)
        p.start_command()  # Start sequence-2 of the function calls.
        p.add_args(c1, c2)
        p.add_args(d1, d2)
        p.run(pool)
        -> run in parallel Thread-1: -> fn(a1, a2); fn(b1, b2)
                           Thread-2: -> fn(c1, c2); fn(d1, d2)
    """

    def __init__(self, fn, preprocess_args_fn=None, handle_errors=False, verbose=False):
        self.fn = fn
        self.args = []
        self.preprocess_args_fn = preprocess_args_fn
        # Whether or not we will throw an error on a cmd failure, or handle it and return a
        # tuple: ('failed-cmd', handle).
        self.handle_errors = handle_errors
        self.verbose = verbose

    # Handle is returned on failed command if handle_errors=true.
    # Example handle is a tuple of (tablet_id, tserver_ip).
    def start_command(self, handle):
        # Start new set of argument tuples.
        # Place handle at the front.
        self.args.append([handle])

    def add_args(self, *args_tuple):
        assert isinstance(args_tuple, tuple)
        assert len(self.args) > 0, 'Call start_command() before'
        self.args[-1].append(args_tuple)

    def run(self, pool):
        def internal_fn(list_of_arg_tuples):
            if self.verbose:
                utc_time = datetime.utcfromtimestamp(time.time())
                logging.info(f'Method {self.fn.__name__} with args {list_of_arg_tuples} \
                            queued at {utc_time}')
            assert isinstance(list_of_arg_tuples, list)
            # First entry is the handle.
            handle = list_of_arg_tuples[0]
            # Pre-process the list of arguments.
            processed_arg_tuples = (list_of_arg_tuples[1:] if self.preprocess_args_fn is None
                                    else self.preprocess_args_fn(list_of_arg_tuples[1:], handle))

            results = []
            for args_tuple in processed_arg_tuples:
                assert isinstance(args_tuple, tuple)
                try:
                    results.append(self.fn(*args_tuple))
                except Exception as ex:
                    logging.warning(
                        "Encountered error for handle '{}' while running command '{}'. Error: {}".
                        format(handle, args_tuple, ex))
                    if (self.handle_errors):
                        # If we handle errors, then return 'failed-cmd' with the handle.
                        return ('failed-cmd', handle)
                    raise ex

            return results

        fn_args = [str(list_of_arg_tuples) for list_of_arg_tuples in self.args]
        return self._run_internal(internal_fn, self.args, fn_args, pool)


def check_arg_range(min_value, max_value):
    """
    Return a "checker" function that validates that an argument is within the given range. To be
    used with argparse.
    """
    def check_fn(value):
        value = int(value)
        if value < min_value or value > max_value:
            raise argparse.ArgumentTypeError("Expected a value between {} and {}, got {}".format(
                min_value, max_value, value))
        return value

    return check_fn


def check_uuid(uuid_str):
    """
    A UUID validator for use with argparse.
    """
    if not UUID_ONLY_RE.match(uuid_str):
        raise argparse.ArgumentTypeError("Expected a UUID, got {}".format(uuid_str))
    return uuid_str


def random_string(length):
    return ''.join(random.choice(string.ascii_lowercase) for i in range(length))


def replace_last_substring(s, old, new):
    return new.join(s.rsplit(old, 1)) if s else None


def strip_dir(dir_path):
    return dir_path.rstrip('/\\')


# TODO: get rid of this sed / test program generation in favor of a more maintainable solution.
def key_and_file_filter(checksum_file):
    return "\" $( sed 's| .*/| |' {} ) \"".format(pipes.quote(checksum_file))


# error_on_failure: If set to true, then the test command will return an error (errno != 0) if the
# check fails. This is useful if we're taking advantage of larger retry mechanisms (eg retrying an
# entire command chain).
# TODO: get rid of this sed / test program generation in favor of a more maintainable solution.
def compare_checksums_cmd(checksum_file1, checksum_file2, error_on_failure=False):
    return "test {} = {}{}".format(
        key_and_file_filter(checksum_file1),
        key_and_file_filter(checksum_file2),
        '' if error_on_failure else ' && echo correct || echo invalid')


def get_db_name_cmd(dump_file):
    return r"sed -n 's/CREATE DATABASE\(.*\)WITH.*/\1/p' " + pipes.quote(dump_file)


def apply_sed_edit_reg_exp_cmd(dump_file, reg_exp):
    return r"sed -i -e $'{}' {}".format(reg_exp, pipes.quote(dump_file))


def replace_db_name_cmd(dump_file, old_name, new_name):
    return apply_sed_edit_reg_exp_cmd(
        dump_file,
        # Replace in YSQLDump:
        #     CREATE DATABASE old_name ...                     -> CREATE DATABASE new_name ...
        #     ALTER DATABASE old_name ...                      -> ALTER DATABASE new_name ...
        #     \connect old_name                                -> \connect new_name
        #     \connect "old_name"                              -> \connect new_name
        #     \connect -reuse-previous=on "dbname='old_name'"  -> \connect new_name
        r's|DATABASE {0}|DATABASE {1}|;'
        r's|\\\\connect {0}|\\\\connect {1}|;'
        r's|\\\\connect {2}|\\\\connect {1}|;'
        r's|\\\\connect -reuse-previous=on \\"dbname=\x27{2}\x27\\"|\\\\connect {1}|'.format(
                old_name, new_name, old_name.replace('"', "")))


def get_table_names_str(keyspaces, tables, delimeter, space):
    if len(keyspaces) != len(tables):
        raise BackupException(
            "Found {} --keyspace keys and {} --table keys. Number of these keys "
            "must be equal.".format(len(keyspaces), len(tables)))

    table_names = []
    for i in range(0, len(tables)):
        table_names.append(delimeter.join([keyspaces[i], tables[i]]))

    return space.join(table_names)


def keyspace_type(keyspace):
    return 'ysql' if ('.' in keyspace) and (keyspace.split('.')[0].lower() == 'ysql') else 'ycql'


def is_parent_table_name(table_name):
    return table_name.endswith(COLOCATED_NAME_SUFFIX) or \
           table_name.endswith(TABLEGROUP_NAME_SUFFIX) or \
           table_name.endswith(COLOCATION_NAME_SUFFIX)


def get_postgres_oid_from_table_id(table_id):
    # Table oid occupies the last 4 bytes in table UUID
    return int(table_id[-8:], 16)


def verify_tablegroup_parent_table_ids(old_id, new_id):
    # Perform check on tablegroup parent tables
    if old_id.endswith(TABLEGROUP_UUID_SUFFIX) or old_id.endswith(COLOCATION_UUID_SUFFIX):
        # Assert that the postgres tablegroup oids are the same.
        old_oid = get_postgres_oid_from_table_id(old_id[:32])
        new_oid = get_postgres_oid_from_table_id(new_id[:32])
        if (old_oid != new_oid):
            raise BackupException('Tablegroup parent table have different tablegroup oids: '
                                  'Old oid: {}, New oid: {}'.format(old_oid, new_oid))


def keyspace_name(keyspace):
    return keyspace.split('.')[1] if ('.' in keyspace) else keyspace


def time_to_str(time_value):
    return time.strftime("%a, %d %b %Y %H:%M:%S +0000", time.gmtime(time_value))


def get_yb_backup_version_string():
    str = "unknown"
    try:
        if os.path.isfile(PLATFORM_VERSION_FILE_PATH):
            with open(PLATFORM_VERSION_FILE_PATH, 'r') as f:
                ver_info = json.load(f)

            str = "version {}".format(ver_info['version_number'])
            if 'build_number' in ver_info:
                str += " build {}".format(ver_info['build_number'])
            if 'git_hash' in ver_info:
                str += " revision {}".format(ver_info['git_hash'])
            if 'build_type' in ver_info:
                str += " build_type {}".format(ver_info['build_type'])
            if 'build_timestamp' in ver_info:
                str += " built at {}".format(ver_info['build_timestamp'])
        else:
            logging.warning("[app] Version file not found: {}".format(PLATFORM_VERSION_FILE_PATH))
    except Exception as ex:
        logging.warning("[app] Cannot parse JSON version file {}: {}".
                        format(PLATFORM_VERSION_FILE_PATH, ex))
    finally:
        return str


class BackupOptions:
    def __init__(self, args):
        self.args = args


class AbstractBackupStorage(object):
    def __init__(self, options):
        self.options = options

    @staticmethod
    def storage_type():
        raise BackupException("Unimplemented")

    def _command_list_prefix(self):
        return []


class AzBackupStorage(AbstractBackupStorage):
    def __init__(self, options):
        super(AzBackupStorage, self).__init__(options)

    @staticmethod
    def storage_type():
        return 'az'

    def _command_list_prefix(self):
        return "azcopy"

    def clean_up_logs_cmd(self):
        return ["{} {} {}".format(self._command_list_prefix(), "jobs", "clean")]

    def upload_file_cmd(self, src, dest, local=False):
        if local is True:
            dest = dest + os.getenv('AZURE_STORAGE_SAS_TOKEN')
            return [self._command_list_prefix(), "cp", src, dest]
        src = "'{}'".format(src)
        dest = "'{}'".format(dest + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        return ["{} {} {} {}".format(self._command_list_prefix(), "cp", src, dest)]

    def download_file_cmd(self, src, dest, local=False):
        if local is True:
            src = src + os.getenv('AZURE_STORAGE_SAS_TOKEN')
            return [self._command_list_prefix(), "cp", src,
                    dest, "--recursive"]
        src = "'{}'".format(src + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        dest = "'{}'".format(dest)
        return ["{} {} {} {} {}".format(self._command_list_prefix(), "cp", src,
                dest, "--recursive")]

    def upload_dir_cmd(self, src, dest):
        # azcopy will download the top-level directory as well as the contents without "/*".
        src = "'{}'".format(os.path.join(src, '*'))
        dest = "'{}'".format(dest + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        return ["{} {} {} {} {}".format(self._command_list_prefix(), "cp", src,
                dest, "--recursive")]

    def download_dir_cmd(self, src, dest):
        src = "'{}'".format(os.path.join(src, '*') + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        dest = "'{}'".format(dest)
        return ["{} {} {} {} {}".format(self._command_list_prefix(), "cp", src,
                dest, "--recursive")]

    def delete_obj_cmd(self, dest):
        if dest is None or dest == '/' or dest == '':
            raise BackupException("Destination needs to be well formed.")
        dest = "'{}'".format(dest + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        return ["{} {} {} {}".format(self._command_list_prefix(), "rm", dest, "--recursive=true")]

    def backup_obj_size_cmd(self, backup_obj_location):
        sas_token = os.getenv('AZURE_STORAGE_SAS_TOKEN')
        backup_obj_location = "'{}'".format(backup_obj_location + sas_token)
        return ["{} {} {} {} {} {} {} {} {}".format(self._command_list_prefix(), "list",
                backup_obj_location, "--machine-readable", "--running-tally", "|",
                "grep 'Total file size:'", "|", "grep -Eo '[0-9]*'")]


class GcsBackupStorage(AbstractBackupStorage):
    def __init__(self, options):
        super(GcsBackupStorage, self).__init__(options)

    @staticmethod
    def storage_type():
        return 'gcs'

    def _command_list_prefix(self):
        return ['gsutil', '-o',
                'Credentials:gs_service_key_file=%s' % self.options.cloud_cfg_file_path]

    def upload_file_cmd(self, src, dest):
        return self._command_list_prefix() + ["cp", src, dest]

    def download_file_cmd(self, src, dest):
        return self._command_list_prefix() + ["cp", src, dest]

    def upload_dir_cmd(self, src, dest):
        if self.options.args.disable_parallelism:
            return self._command_list_prefix() + ["rsync", "-r", src, dest]
        else:
            return self._command_list_prefix() + ["-m", "rsync", "-r", src, dest]

    def download_dir_cmd(self, src, dest):
        return self._command_list_prefix() + ["-m", "rsync", "-r", src, dest]

    def delete_obj_cmd(self, dest):
        if dest is None or dest == '/' or dest == '':
            raise BackupException("Destination needs to be well formed.")
        return self._command_list_prefix() + ["rm", "-r", dest]

    def backup_obj_size_cmd(self, backup_obj_location):
        return self._command_list_prefix() + ["du", "-s", "-a", backup_obj_location]


class S3BackupStorage(AbstractBackupStorage):
    def __init__(self, options):
        super(S3BackupStorage, self).__init__(options)

    @staticmethod
    def storage_type():
        return 's3'

    def _command_list_prefix(self):
        # If 's3cmd get' fails it creates zero-length file, '--force' is needed to
        # override this empty file on the next retry-step.
        args = ['s3cmd', '--force', '--no-check-certificate', '--config=%s'
                % self.options.cloud_cfg_file_path]
        if self.options.args.disable_multipart:
            args.append('--disable-multipart')
        if self.options.access_token:
            args.append('--access_token=%s' % self.options.access_token)
        return args

    def upload_file_cmd(self, src, dest):
        cmd_list = ["put", src, dest]
        if self.options.args.sse:
            cmd_list.append("--server-side-encryption")
        return self._command_list_prefix() + cmd_list

    def download_file_cmd(self, src, dest):
        return self._command_list_prefix() + ["get", src, dest]

    def upload_dir_cmd(self, src, dest):
        cmd_list = ["sync", "--no-check-md5", src, dest]
        if self.options.args.sse:
            cmd_list.append("--server-side-encryption")
        return self._command_list_prefix() + cmd_list

    def download_dir_cmd(self, src, dest):
        return self._command_list_prefix() + ["sync", "--no-check-md5", src, dest]

    def delete_obj_cmd(self, dest):
        if dest is None or dest == '/' or dest == '':
            raise BackupException("Destination needs to be well formed.")
        return self._command_list_prefix() + ["del", "-r", dest]

    def backup_obj_size_cmd(self, backup_obj_location):
        return self._command_list_prefix() + ["du", backup_obj_location]


class NfsBackupStorage(AbstractBackupStorage):
    def __init__(self, options):
        super(NfsBackupStorage, self).__init__(options)

    @staticmethod
    def storage_type():
        return 'nfs'

    def _command_list_prefix(self):
        result = ['rsync', '-avhW']
        if not self.options.args.mac:
            result.append('--no-compress')
        return result

    # This is a single string because that's what we need for doing `mkdir && rsync`.
    def upload_file_cmd(self, src, dest):
        return ["mkdir -p {} && {} {} {}".format(
            pipes.quote(os.path.dirname(dest)), " ".join(self._command_list_prefix()),
            pipes.quote(src), pipes.quote(dest))]

    def download_file_cmd(self, src, dest):
        return self._command_list_prefix() + [src, dest]

    # This is a list of single string, because a) we need a single string for executing
    # `mkdir && rsync` and b) we need a list of 1 element, as it goes through a tuple().
    def upload_dir_cmd(self, src, dest):
        return ["mkdir -p {} && {} {} {}".format(
            pipes.quote(dest), " ".join(self._command_list_prefix()),
            pipes.quote(src), pipes.quote(dest))]

    def download_dir_cmd(self, src, dest):
        if self.options.args.TEST_sleep_during_download_dir:
            return ["sleep 5 && {} {} {}".format(
                " ".join(self._command_list_prefix()), pipes.quote(src), pipes.quote(dest))]
        return self._command_list_prefix() + [src, dest]

    def delete_obj_cmd(self, dest):
        if dest is None or dest == '/' or dest == '':
            raise BackupException("Destination needs to be well formed.")
        return ["rm", "-rf", pipes.quote(dest)]

    def backup_obj_size_cmd(self, backup_obj_location):
        # On MAC 'du -sb' does not work: '-b' is not supported.
        # It's possible to use another approach: size = 'du -sk'*1024.
        # But it's not implemented because MAC is not the production platform now.
        return ["du", "-sb", backup_obj_location]


BACKUP_STORAGE_ABSTRACTIONS = {
    S3BackupStorage.storage_type(): S3BackupStorage,
    NfsBackupStorage.storage_type(): NfsBackupStorage,
    GcsBackupStorage.storage_type(): GcsBackupStorage,
    AzBackupStorage.storage_type(): AzBackupStorage
}


class KubernetesDetails():
    def __init__(self, server_ip, config_map):
        self.pod_name = config_map[server_ip]["podName"]
        self.namespace = config_map[server_ip]["namespace"]
        # The pod names are <helm fullname>yb-<master|tserver>-n where
        # n is the pod number. <helm fullname> can be blank. And
        # yb-master/yb-tserver are the container names.
        self.container = "yb-master" if self.pod_name.find("master") > 0 else "yb-tserver"
        self.env_config = os.environ.copy()
        self.env_config["KUBECONFIG"] = config_map[server_ip]["KUBECONFIG"]


def get_instance_profile_credentials():
    # The version of boto we use conflicts with later versions of python3.  Hide the boto imports
    # inside the sole function that uses boto so unit tests are not affected.
    # This function is only used by the s3 code path which unit tests do not use.
    from boto.utils import get_instance_metadata
    from botocore.session import get_session
    from botocore.credentials import get_credentials
    result = ()
    iam_credentials_endpoint = 'meta-data/iam/security-credentials/'
    metadata = get_instance_metadata(timeout=1, num_retries=1, data=iam_credentials_endpoint)
    if metadata:
        instance_credentials = next(iter(metadata.values()))
        if isinstance(instance_credentials, dict):
            try:
                access_key = instance_credentials['AccessKeyId']
                secret_key = instance_credentials['SecretAccessKey']
                token = instance_credentials['Token']
                result = access_key, secret_key, token
            except KeyError as e:
                logging.info("Could not find {} in instance metadata".format(e))
    else:
        # Get credentials using session token for IMDSv2
        session = get_session()
        credentials = get_credentials(session)
        if isinstance(credentials, object):
            result = credentials.access_key, credentials.secret_key, credentials.token
        else:
            raise BackupException("Failed to retrieve IAM role data from AWS")

    return result


class YBVersion:
    def __init__(self, ver_str, verbose=False):
        self.string = ver_str.strip()
        if verbose:
            logging.info("YB cluster version string: " + self.string)

        matched = YB_VERSION_RE.match(self.string)
        self.parts = matched.group(1).split('.') if matched else None
        if self.parts:
            logging.info("[app] YB cluster version: " + '.'.join(self.parts))


class YBTSConfig:
    """
    Helper class to store TS configuration parameters.
    """
    FS_DATA_DIRS_ARG = 'fs_data_dirs'
    PLACEMENT_REGION_ARG = 'placement_region'
    WEBSERVER_PORT_ARG = 'webserver_port'

    FS_DATA_DIRS_ARG_PREFIX = '--' + FS_DATA_DIRS_ARG + '='
    PLACEMENT_REGION_RE = re.compile(r'[\S\s]*--' + PLACEMENT_REGION_ARG + r'=([\S]*)')

    def __init__(self, backup):
        self.backup = backup
        self.params = {}
        self.clean()

    def clean(self):
        # Clean this TS config (keep only web-port).
        if self.WEBSERVER_PORT_ARG in self.params:
            web_port = self.params[self.WEBSERVER_PORT_ARG]
        else:
            web_port = DEFAULT_TS_WEB_PORT

        self.params = {}
        self.set_web_port(web_port)

    def has_data_dirs(self):
        return self.FS_DATA_DIRS_ARG in self.params

    def data_dirs(self):
        return self.params[self.FS_DATA_DIRS_ARG]

    def has_region(self):
        return self.PLACEMENT_REGION_ARG in self.params

    def region(self):
        return self.params[self.PLACEMENT_REGION_ARG]

    def set_web_port(self, web_port):
        self.params[self.WEBSERVER_PORT_ARG] = web_port

    def load(self, tserver_ip, read_region=False):
        """
        Load TS properties for this TS IP via TS web-interface.
        :param tserver_ip: tablet server ip
        :param read_region: parse --placement_region value from TS configuration
        """
        self.clean()
        web_port = self.params[self.WEBSERVER_PORT_ARG]

        if self.backup.args.verbose:
            logging.info("Loading TS config via Web UI on {}:{}".format(tserver_ip, web_port))

        url = "{}:{}/varz?raw=1".format(tserver_ip, web_port)
        output = self.backup.run_program(
            ['curl', url, '--silent', '--show-error', '--insecure', '--location'], num_retry=10)

        # Read '--placement_region'.
        if read_region:
            suffix_match = self.PLACEMENT_REGION_RE.match(output)
            if suffix_match is None:
                msg = "Cannot get region from {}: [[ {} ]]".format(url, output)
                logging.error("[app] {}".format(msg))
                self.clean()
                raise BackupException(msg)

            region = suffix_match.group(1)
            logging.info("[app] Region for TS IP {} is '{}'".format(tserver_ip, region))
            self.params[self.PLACEMENT_REGION_ARG] = region

        # Read '--fs_data_dirs'.
        data_dirs = []
        for line in output.split('\n'):
            if line.startswith(self.FS_DATA_DIRS_ARG_PREFIX):
                for data_dir in line[len(self.FS_DATA_DIRS_ARG_PREFIX):].split(','):
                    data_dir = data_dir.strip()
                    if data_dir:
                        data_dirs.append(data_dir)
                break

        if not data_dirs:
            msg = "Did not find any data directories in tserver by querying /varz?raw=1 endpoint"\
                  " on tserver '{}:{}'. Was looking for '{}', got this: [[ {} ]]".format(
                      tserver_ip, web_port, self.FS_DATA_DIRS_ARG_PREFIX, output)
            logging.error("[app] {}".format(msg))
            self.clean()
            raise BackupException(msg)
        elif self.backup.args.verbose:
            logging.info("Found data directories on tablet server '{}': {}".format(
                tserver_ip, data_dirs))

        self.params[self.FS_DATA_DIRS_ARG] = data_dirs


class YBManifest:
    """
    Manifest is the top level JSON-based file-descriptor with the backup content and properties.
    The class is responsible for the data initialization, loading/saving from/into a JSON file.
    The data can be initialized by default/stub values for old backup format (version='0').

    The format can be extended. For example:
      - metadata files can be listed in the locations
      - checksum value can be stored for any tablet-folder
      - any tablet-folder can include complete list of SST files

    Format:
      {
        # This Manifest file format version.
        "version": "1.0",

        # Source cluster parameters.
        "source": {
          # Array of Master IPs.
          "yb-masters": [ "127.0.0.1", ... ],

          # Array of table names in the backup.
          "tables": [ "ysql.yugabyte.tbl1", "ysql.yugabyte.tbl2", ... ],

          # Is it YSQL backup?
          "ysql": true,

          # Is YSQL authentication enabled?
          "ysql_authentication": false,

          # Is Kubernetes used?
          "k8s": false,

          # YB cluster version string. Usually it includes:
          # version_number, build_number, git_hash, build_type, build_timestamp.
          "yb-database-version": "version 2.11.2.0 build PRE_RELEASE ..."
        },

        # Properties of the created backup.
        "properties": {
          # The backup creation start/finish time.
          "start-time": "Mon, 06 Dec 2021 20:45:25 +0000",
          "end-time": "Mon, 06 Dec 2021 20:45:54 +0000",

          # The backup script version string.
          "platform-version": "version 2.11.2.0 build PRE_RELEASE ...",

          # Target storage provider (AWS/GCP/NFS/etc.).
          "storage-type": "nfs",

          # Was additional 'YSQLDump_tablespaces' dump file created?
          "backup-tablespaces": true,

          # Parallelism level - number of worker threads.
          "parallelism": 8,

          # True for pg dump based backups.
          "pg_based_backup": false
        },

        # Content of the backup folders/locations.
        "locations": {
          # Single main location.
          <default_backup_location>: {
            # Default location marker.
            "default": true,

            # Tablet folders in the location.
            "tablet-directories": {
              <tablet_id>: {},
              ...
            },
          },

          # Multiple regional locations.
          <regional_location>: {
            # The region name.
            "region": <region_name>,

            # Tablet folders in the location.
            "tablet-directories": {
              <tablet_id>: {},
              ...
            }
          },
          ...
        }
      }
    """

    def __init__(self, backup):
        self.backup = backup
        self.body = {}
        self.body['version'] = "0"

    # Data initialization methods.
    def init(self, snapshot_bucket, pg_based_backup):
        # Call basic initialization by default to prevent code duplication.
        self.create_by_default(self.backup.snapshot_location(snapshot_bucket))
        self.body['version'] = "1.0"

        # Source cluster parameters.
        source = {}
        self.body['source'] = source
        source['ysql'] = self.backup.is_ysql_keyspace()
        source['ysql_authentication'] = self.backup.args.ysql_enable_auth
        source['k8s'] = self.backup.is_k8s()
        source['yb-database-version'] = self.backup.database_version.string
        source['yb-masters'] = self.backup.args.masters.split(",")
        if not pg_based_backup:
            source['tables'] = self.backup.table_names_str().split(" ")

        # The backup properties.
        properties = self.body['properties']
        properties['platform-version'] = get_yb_backup_version_string()
        properties['parallelism'] = self.backup.args.parallelism
        if not ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR:
            properties['use-tablespaces'] = self.backup.args.use_tablespaces
        properties['backup-tablespaces'] = self.backup.args.backup_tablespaces
        properties['backup-roles'] = self.backup.args.backup_roles
        properties['pg-based-backup'] = pg_based_backup
        properties['start-time'] = self.backup.timer.start_time_str()
        properties['end-time'] = self.backup.timer.end_time_str()
        properties['size-in-bytes'] = self.backup.calc_size_in_bytes(snapshot_bucket)
        properties['check-sums'] = not self.backup.args.disable_checksums
        if not self.backup.args.disable_checksums:
            properties['hash-algorithm'] = XXH64_FILE_EXT \
                if self.backup.xxhash_checksum_path else SHA_FILE_EXT

    def init_locations(self, tablet_leaders, snapshot_bucket):
        locations = self.body['locations']
        for (tablet_id, leader_ip, tserver_region) in tablet_leaders:
            tablet_location = self.backup.snapshot_location(snapshot_bucket, tserver_region)
            if tablet_location not in locations:
                # Init the regional location. Single main location was added in init().
                assert tablet_location != self.backup.args.backup_location
                location_data = {}
                locations[tablet_location] = location_data
                location_data['tablet-directories'] = {}
                location_data['region'] = tserver_region

            locations[tablet_location]['tablet-directories'][tablet_id] = {}

        self.body['properties']['size-in-bytes'] += len(self.to_string())

    # Data saving/loading/printing.
    def to_string(self):
        return json.dumps(self.body, indent=2)

    def save_into_file(self, file_path):
        logging.info('[app] Exporting manifest data to {}'.format(file_path))
        with open(file_path, 'w') as f:
            f.write(self.to_string())

    def load_from_file(self, file_path):
        logging.info('[app] Loading manifest data from {}'.format(file_path))
        with open(file_path, 'r') as f:
            self.body = json.load(f)

    def is_loaded(self):
        return self.body['version'] != "0"

    # Stub methods for the old backup format. There is no real Manifest file in the backup.
    def create_by_default(self, default_backup_location):
        properties = {}
        self.body['properties'] = properties
        properties['storage-type'] = self.backup.args.storage_type

        self.body['locations'] = {}
        default_location_data = {}
        # Only one single main location is available in the old backup.
        self.body['locations'][default_backup_location] = default_location_data
        default_location_data['default'] = True
        default_location_data['tablet-directories'] = {}

    # Helper data getters.
    def get_tablet_locations(self, tablet_location):
        assert not tablet_location  # Empty.
        locations = self.body['locations']
        for loc in locations:
            for tablet_id in locations[loc]['tablet-directories']:
                tablet_location[tablet_id] = loc

    def is_pg_based_backup(self):
        assert 'properties' in self.body
        return self.body['properties'].get('pg-based-backup', False)

    def get_backup_size(self):
        return self.body['properties'].get('size-in-bytes')

    def get_locations(self):
        return self.body['locations'].keys()

    def get_hash_algorithm(self):
        assert 'properties' in self.body
        return self.body['properties'].get('hash-algorithm', SHA_FILE_EXT)


class YBBackup:
    def __init__(self):
        signal.signal(signal.SIGINT, self.cleanup_on_exit)
        signal.signal(signal.SIGTERM, self.cleanup_on_exit)
        signal.signal(signal.SIGQUIT, self.cleanup_on_exit)
        self.pools = []
        self.leader_master_ip = ''
        self.ysql_ip = ''
        self.live_tserver_ip = ''
        self.tmp_dir_name = ''
        self.server_ips_with_uploaded_cloud_cfg = {}
        self.k8s_pod_addr_to_cfg = {}
        self.timer = BackupTimer()
        self.ts_cfgs = {}
        self.ip_to_ssh_key_map = {}
        self.secondary_to_primary_ip_map = {}
        self.broadcast_to_rpc_map = {}
        self.rpc_to_broadcast_map = {}
        self.region_to_location = {}
        self.xxhash_checksum_path = ''
        self.database_version = YBVersion("unknown")
        self.manifest = YBManifest(self)
        self.parse_arguments()

    def cleanup_on_exit(self, signum, frame):
        self.terminate_pools()
        # Runs clean-up callbacks registered to atexit.
        sys.exit()

    def terminate_pools(self):
        for pool in self.pools:
            logging.info("Terminating threadpool ...")
            try:
                pool.close()
                pool.terminate()
                pool.join()
                logging.info("Terminated threadpool ...")
            except Exception as ex:
                logging.error("Failed to terminate pool: {}".format(ex))

    def sleep_or_raise(self, num_retry, timeout, ex):
        if num_retry > 0:
            logging.info("Sleep {}... ({} retries left)".format(timeout, num_retry))
            time.sleep(timeout)
        else:
            raise ex

    def run_program(self, args, num_retry=1, timeout=10, env=None,
                    redirect_stderr=subprocess.STDOUT, **kwargs):
        """
        Runs the given program with the given set of arguments. Arguments are the same as in
        subprocess.check_output. Logs the program and the output in verbose mode. Also logs the
        command line in case of failure.
        """
        cmd_as_str = quote_cmd_line_for_bash(args)
        if self.args.verbose:
            logging.info("Running command{}: {}".format(
                "" if num_retry == 1 else " ({} retries)".format(num_retry), cmd_as_str))

        while num_retry > 0:
            num_retry = num_retry - 1

            try:
                proc_env = os.environ.copy()
                proc_env.update(env if env is not None else {})

                subprocess_result = str(subprocess.check_output(
                    args, stderr=redirect_stderr,
                    env=proc_env, **kwargs).decode('utf-8', errors='replace')
                    .encode("ascii", "ignore")
                    .decode("ascii"))

                if self.args.verbose:
                    logging.info(
                        "Output from running command [[ {} ]]:\n{}\n[[ END OF OUTPUT ]]".format(
                            cmd_as_str, subprocess_result))
                return subprocess_result
            except subprocess.CalledProcessError as e:
                log_out = e.output if (redirect_stderr == subprocess.STDOUT) else e.stderr
                logging.error("Failed to run command [[ {} ]]: code={} output={}".format(
                    cmd_as_str, e.returncode, str(log_out.decode('utf-8', errors='replace')
                                                         .encode("ascii", "ignore")
                                                         .decode("ascii"))))
                self.sleep_or_raise(num_retry, timeout, e)
            except Exception as ex:
                logging.error("Failed to run command [[ {} ]]: {}".format(cmd_as_str, ex))
                self.sleep_or_raise(num_retry, timeout, ex)

    def parse_arguments(self):
        parser = argparse.ArgumentParser(
            description='Backup/restore YB table',
            epilog="Use the following environment variables to provide AWS access and secret "
                   "keys for S3:\n"
                   "    export AWS_ACCESS_KEY_ID=<your_aws_access_key>\n"
                   "    export AWS_SECRET_ACCESS_KEY=<your_aws_secret_key>\n"
                   "For GCS:\n"
                   "    export GCS_CREDENTIALS_JSON=<contents_of_gcp_credentials>\n"
                   "For YCQL tables:\n"
                   "    Keys --keyspace, --table and --table_uuid can be repeated several times.\n"
                   "    Recommended order for creating backup: --keyspace ks1 --table tbl1 "
                   "    --table_uuid uuid1 --keyspace ks2 --table tbl2 --table_uuid uuid2 ...\n"
                   "    Recommended order for restoring backup: --keyspace target_ks --table tbl1 "
                   "    --table tbl2 ...\n"
                   "For YSQL DB:\n"
                   "    Only one key --keyspace is supported. The script processes the whole DB.\n"
                   "    For creating backup: --keyspace ysql.db1\n"
                   "    For restoring backup: --keyspace ysql.db1_copy\n",
            formatter_class=RawDescriptionHelpFormatter)

        parser.add_argument(
            '--masters', required=True,
            help="Comma separated list of masters for the cluster")
        parser.add_argument(
            '--ts_web_hosts_ports', help="Custom TS HTTP hosts and ports. "
                                         "In form: <IP>:<Port>,<IP>:<Port>")
        parser.add_argument(
            # Keeping the "ts_" prefix in the name for backward compatibility only.
            # In fact this is IP mapping for TServers and Masters.
            '--ts_secondary_ip_map', default=None,
            help="Map of secondary IPs to primary for ensuring ssh connectivity")
        parser.add_argument(
            '--ip_to_ssh_key_path', default=None,
            help="Map of IPs to their SSH keys")
        parser.add_argument(
            '--k8s_config', required=False,
            help="Namespace to use for kubectl in case of kubernetes deployment")
        parser.add_argument(
            '--keyspace', action='append', help="Repeatable keyspace of the tables to backup, "
                                                "or a target keyspace for the backup restoring")
        parser.add_argument(
            '--table', action='append',
            help="Repeatable name of the tables to backup or restore")
        parser.add_argument(
            '--table_uuid', action='append',
            help="Repeatable UUID of the tables to backup.")
        parser.add_argument(
            '--local_yb_admin_binary', help="Path to the local yb-admin binary; "
                                            "by default remote yb-admin tool is used")
        parser.add_argument(
            '--remote_yb_admin_binary', default=DEFAULT_REMOTE_YB_ADMIN_PATH,
            help="Path to the remote yb-admin binary")
        parser.add_argument(
            '--local_ysql_dump_binary', help="Path to the local ysql_dump binary; "
                                             "by default remote ysql_dump tool is used")
        parser.add_argument(
            '--remote_ysql_dump_binary', default=DEFAULT_REMOTE_YSQL_DUMP_PATH,
            help="Path to the remote ysql_dump binary")
        parser.add_argument(
            '--local_ysql_shell_binary', help="Path to the local ysql shell binary; "
                                              "by default remote ysql shell tool is used")
        parser.add_argument(
            '--remote_ysql_shell_binary', default=DEFAULT_REMOTE_YSQL_SHELL_PATH,
            help="Path to the remote ysql shell binary")
        parser.add_argument(
            '--pg_based_backup', action='store_true', default=False,
            help="Use it to trigger pg based backup.")
        parser.add_argument(
            '--ysql_ignore_restore_errors', action='store_true', default=False,
            help="Do NOT use ON_ERROR_STOP mode when applying YSQL dumps in backup-restore.")
        parser.add_argument(
            '--ysql_disable_db_drop_on_restore_errors', action='store_true', default=False,
            help="Do NOT drop just created (or existing) YSQL DB if YSQL dump apply failed.")
        parser.add_argument(
            '--disable_xxhash_checksum', action='store_true', default=False,
            help="Disables xxhash algorithm for checksum computation.")
        parser.add_argument(
            '--disable_multipart', required=False, action='store_true', default=False,
            help="Disable multipart upload for S3 backups")
        parser.add_argument(
            '--ssh_key_path', required=False, help="Path to the ssh key file")
        parser.add_argument(
            '--ssh_user', default=DEFAULT_YB_USER, help="Username to use for the ssh connection.")
        parser.add_argument(
            '--remote_user', default=DEFAULT_YB_USER, help="User that will perform backup tasks.")
        parser.add_argument(
            '--ssh_port', default='54422', help="Port to use for the ssh connection.")
        parser.add_argument(
            '--no_ssh', action='store_true', default=False, help="Don't use SSH to run commands")
        parser.add_argument(
            '--mac', action='store_true', default=False, help="Use MacOS tooling")
        parser.add_argument(
            '--ysql_port', help="Custom YSQL process port. "
                                "Default port is used if not specified.")
        parser.add_argument(
            '--ysql_host', help="Custom YSQL process host. "
                                "First alive TS host is used if not specified.")
        parser.add_argument(
            '--ysql_enable_auth', action='store_true', default=False,
            help="Whether ysql authentication is required. If specified, will connect using local "
                 "UNIX socket as the host. Overrides --local_ysql_dump_binary to always "
                 "use remote binary.")
        parser.add_argument(
            '--disable_checksums', action='store_true', default=False,
            help="Whether checksums will be created and checked. If specified, will skip using "
                 "checksums.")
        parser.add_argument('--useTserver', action='store_true', required=False, default=False,
                            help="use tserver instead of master for backup operations")

        backup_location_group = parser.add_mutually_exclusive_group(required=True)
        backup_location_group.add_argument(
            '--backup_location',
            help="Directory/bucket under which the snapshots should be created or "
                 "an exact snapshot directory in case of snapshot restoring.")
        # Deprecated flag for backwards compatibility.
        backup_location_group.add_argument('--s3bucket', required=False, help=argparse.SUPPRESS)

        parser.add_argument(
            '--region', action='append',
            help="Repeatable region to create geo-partitioned backup. Every '--region' must have "
                 "related '--region_location' value. For 'restore' it's not used.")
        parser.add_argument(
            '--region_location', action='append',
            help="Repeatable directory/bucket for a region. For 'create' mode it should be "
                 "related to a '--region'. For 'restore' it's not used.")

        parser.add_argument('--ssh2_enabled', action='store_true', default=False)
        parser.add_argument(
            '--no_auto_name',
            action='store_true',
            help="Disable automatic generation of a name under the given backup location. If this "
                 "is specified, the backup location will be the exact path of the directory "
                 "storing the snapshot.")
        parser.add_argument(
            '--no_snapshot_deleting',
            action='store_true',
            help="Disable automatic snapshot deleting after the backup creating or restoring.")
        parser.add_argument(
            '--snapshot_id', type=check_uuid,
            help="Use the existing snapshot ID instead of creating a new one.")
        parser.add_argument(
            '--verbose', required=False, action='store_true', help='Verbose mode')
        parser.add_argument(
            '-j', '--parallelism', type=check_arg_range(1, 100), default=8,
            help='Maximum number of parallel commands to launch. '
                 'This also affects the amount of outgoing s3cmd sync traffic when copying a '
                 'backup to S3.')
        parser.add_argument(
            '--disable_parallelism', action='store_true', default=False,
            help="If specified as False, we add the parallelism flag '-m' during gsutil. "
                 "If speciifed as True, the '-m' flag is not added.")
        parser.add_argument(
            '--storage_type', choices=list(BACKUP_STORAGE_ABSTRACTIONS.keys()),
            default=S3BackupStorage.storage_type(),
            help="Storage backing for backups, eg: s3, nfs, gcs, ..")
        parser.add_argument(
            'command', choices=['create', 'restore', 'restore_keys', 'delete'],
            help='Create, restore or delete the backup from the provided backup location.')
        parser.add_argument(
            '--certs_dir', required=False,
            help="The directory containing the certs for secure connections.")
        parser.add_argument(
            '--sse', required=False, action='store_true',
            help='Enable server side encryption on storage')
        parser.add_argument(
            '--backup_keys_source', required=False,
            help="Location of universe encryption keys backup file to upload to backup location"
        )
        parser.add_argument(
            '--restore_keys_destination', required=False,
            help="Location to download universe encryption keys backup file to"
        )
        parser.add_argument(
            '--nfs_storage_path', required=False, help="NFS storage mount path")
        parser.add_argument(
            '--restore_time', required=False,
            help='The Unix microsecond timestamp to which to restore the snapshot.')

        parser.add_argument(
            '--backup_tablespaces', required=False, action='store_true', default=False,
            help='Backup YSQL TABLESPACE objects into the backup.')
        parser.add_argument(
            '--restore_tablespaces', required=False, action='store_true', default=False,
            help='Restore YSQL TABLESPACE objects from the backup.')
        parser.add_argument(
            '--ignore_existing_tablespaces', required=False, action='store_true', default=False,
            help='Ignore the tablespace creation if it already exists.')
        parser.add_argument(
            '--use_tablespaces', required=False, action='store_true', default=False,
            help="Use the restored YSQL TABLESPACE objects for the tables "
                 "via 'SET default_tablespace=...' syntax.")

        parser.add_argument(
            '--backup_roles', required=False, action='store_true', default=False,
            help='Backup YSQL ROLE objects into the backup.')
        parser.add_argument(
            '--restore_roles', required=False, action='store_true', default=False,
            help='Restore YSQL ROLE objects from the backup.')
        parser.add_argument(
            '--ignore_existing_roles', required=False, action='store_true', default=False,
            help='Ignore the role creation if it already exists.')
        parser.add_argument(
            '--use_roles', required=False, action='store_true', default=False,
            help="Use the restored YSQL ROLE objects for the tables "
                 "via 'ALTER TABLE ... OWNER TO <role>' and "
                 "'REVOKE/GRANT ... ON TABLE ... FROM/TO <role>' syntax.")

        parser.add_argument('--upload', dest='upload', action='store_true', default=True)
        # Please note that we have to use this weird naming (i.e. underscore in the argument name)
        # style to keep it in sync with YB processes G-flags.
        parser.add_argument('--no_upload', dest='upload', action='store_false',
                            help="Skip uploading snapshot")
        parser.add_argument(
            '--edit_ysql_dump_sed_reg_exp', required=False,
            help="Regular expression for 'sed' tool to edit on fly YSQL dump file(s) during the "
                 "backup restoring. Example: \"s|OWNER TO yugabyte|OWNER TO admin|\". WARNING: "
                 "Contact support team before use! No any backward compatibility guaranties.")
        parser.add_argument(
            '--do_not_disable_splitting', required=False, action='store_true', default=False,
            help="Do not disable automatic splitting before taking a backup. This is dangerous "
                 "because a tablet might be split and cleaned up just before we try to copy its "
                 "data.")
        parser.add_argument(
            '--use_server_broadcast_address', required=False, action='store_true', default=False,
            help="Use server_broadcast_address if available, otherwise use rpc_bind_address. Note "
                 "that broadcast address is overwritten by 'ts_secondary_ip_map' if available."
        )
        # Do not include indexes when creating snapshots.
        parser.add_argument(
            '--skip_indexes', required=False, action='store_true',
            default=False,
            help="Don't snapshot or backup Indexes. This gives the user an option to "
            "avoid the overheads of snapshotting and uploading indexes. Instead, pay the cost "
            "of index backfills at the time of restore. Applicable only for YCQL.")

        """
        Test arguments
        - Use `argparse.SUPPRESS` to keep these arguments hidden.
        """
        # Adds in a sleep before the rsync command to download data during the restore. Used in
        # tests to hit tablet move race conditions during restores.
        parser.add_argument(
            '--TEST_sleep_during_download_dir', required=False, action='store_true', default=False,
            help=argparse.SUPPRESS)

        # Adds in a sleep after finding the list of snapshot directories to upload but before
        # uploading them, to test that they are not deleted by a completed tablet split (tablet
        # splitting should be disabled).
        parser.add_argument(
            '--TEST_sleep_after_find_snapshot_dirs', required=False, action='store_true',
            default=False, help=argparse.SUPPRESS)

        # Simulate an older yb-admin which does not support some command.
        parser.add_argument(
            '--TEST_yb_admin_unsupported_commands', required=False, action='store_true',
            default=False, help=argparse.SUPPRESS)

        # Do not fsync for unit tests.
        parser.add_argument(
            '--TEST_never_fsync', required=False, action='store_true',
            default=False, help=argparse.SUPPRESS)

        parser.add_argument(
            '--TEST_drop_table_before_upload', required=False, action='store_true',
            default=False, help=argparse.SUPPRESS)

        self.args = parser.parse_args()

    def post_process_arguments(self):
        if self.args.verbose:
            logging.info("Parsed arguments: {}".format(vars(self.args)))

        if self.is_k8s():
            self.k8s_pod_addr_to_cfg = json.loads(self.args.k8s_config)
            if self.k8s_pod_addr_to_cfg is None:
                raise BackupException("Couldn't load k8s configs")

        if self.args.storage_type == 'nfs':
            logging.info('Checking whether NFS backup storage path mounted on TServers or not')
            with terminating(ThreadPool(self.args.parallelism)) as pool:
                self.pools.append(pool)
                tserver_ips = self.get_live_tservers()
                SingleArgParallelCmd(self.find_nfs_storage,
                                     tserver_ips, verbose=self.args.verbose).run(pool)

        self.args.backup_location = self.args.backup_location or self.args.s3bucket
        options = BackupOptions(self.args)
        self.cloud_cfg_file_path = os.path.join(self.get_tmp_dir(), CLOUD_CFG_FILE_NAME)
        if self.is_s3():
            access_token = None
            proxy_config = ''
            if os.getenv('PROXY_HOST'):
                proxy_config = 'proxy_host = ' + os.environ['PROXY_HOST'] + '\n'

                if os.getenv('PROXY_PORT'):
                    proxy_config += 'proxy_port = ' + os.environ['PROXY_PORT'] + '\n'

            host_base = os.getenv('AWS_HOST_BASE')
            path_style_access = True if os.getenv('PATH_STYLE_ACCESS',
                                                  "false") == "true" else False
            if host_base:
                bucket = self.args.backup_location
                if not path_style_access:
                    bucket += '.' + host_base

                host_base_cfg = 'host_base = ' + host_base + '\n' \
                                'host_bucket = ' + bucket + '\n'

                if host_base.startswith("http://"):
                    host_base_cfg += 'use_https = false \n'
            else:
                host_base_cfg = ''
            if not os.getenv('AWS_SECRET_ACCESS_KEY') and not os.getenv('AWS_ACCESS_KEY_ID'):
                metadata = get_instance_profile_credentials()
                with open(self.cloud_cfg_file_path, 'w') as s3_cfg:
                    if metadata:
                        access_token = metadata[2]
                        s3_cfg.write('[default]\n' +
                                     'access_key = ' + metadata[0] + '\n' +
                                     'secret_key = ' + metadata[1] + '\n' +
                                     'access_token = ' + metadata[2] + '\n' +
                                     host_base_cfg +
                                     proxy_config)
                    else:
                        s3_cfg.write('[default]\n' +
                                     'access_key = ' + '\n' +
                                     'secret_key = ' + '\n' +
                                     'access_token = ' + '\n' +
                                     host_base_cfg +
                                     proxy_config)
            elif os.getenv('AWS_SECRET_ACCESS_KEY') and os.getenv('AWS_ACCESS_KEY_ID'):
                with open(self.cloud_cfg_file_path, 'w') as s3_cfg:
                    s3_cfg.write('[default]\n' +
                                 'access_key = ' + os.environ['AWS_ACCESS_KEY_ID'] + '\n' +
                                 'secret_key = ' + os.environ['AWS_SECRET_ACCESS_KEY'] + '\n' +
                                 host_base_cfg +
                                 proxy_config)
            else:
                raise BackupException(
                    "Missing either AWS access key or secret key for S3 "
                    "in AWS_ACCESS_KEY_ID or AWS_SECRET_ACCESS_KEY environment variables.")

            os.chmod(self.cloud_cfg_file_path, 0o400)
            options.cloud_cfg_file_path = self.cloud_cfg_file_path
            # access_token: Used when AWS credentials are used from IAM role. It
            # is the identifier for temporary credentials, passed as command-line
            # param with s3cmd, so that s3cmd does not try to refresh credentials
            # on it's own.
            options.access_token = access_token
        elif self.is_gcs():
            credentials = os.getenv('GCS_CREDENTIALS_JSON')
            if not credentials:
                raise BackupException(
                    "Set GCP credential file for GCS in GCS_CREDENTIALS_JSON "
                    "environment variable.")
            with open(self.cloud_cfg_file_path, 'w') as cloud_cfg:
                cloud_cfg.write(credentials)
            options.cloud_cfg_file_path = self.cloud_cfg_file_path
        elif self.is_az():
            sas_token = os.getenv('AZURE_STORAGE_SAS_TOKEN')
            if not sas_token:
                raise BackupException(
                    "Set SAS for Azure Storage in AZURE_STORAGE_SAS_TOKEN environment variable.")
            if '?' not in sas_token:
                raise BackupException(
                    "SAS tokens must begin with '?'.")

        self.storage = BACKUP_STORAGE_ABSTRACTIONS[self.args.storage_type](options)

        if self.args.ts_secondary_ip_map is not None:
            self.secondary_to_primary_ip_map = json.loads(self.args.ts_secondary_ip_map)

        if self.args.ip_to_ssh_key_path is not None:
            self.ip_to_ssh_key_map = json.loads(self.args.ip_to_ssh_key_path)

        self.args.local_ysql_dumpall_binary = replace_last_substring(
            self.args.local_ysql_dump_binary, "ysql_dump", "ysql_dumpall")
        self.args.remote_ysql_dumpall_binary = replace_last_substring(
            self.args.remote_ysql_dump_binary, "ysql_dump", "ysql_dumpall")

        if self.args.ts_web_hosts_ports:
            # The TS Web host/port provided in here is used to hit the /varz endpoint
            # of the tserver. The host can either be RPC IP/Broadcast IP.
            logging.info('TS Web hosts/ports: {}'.format(self.args.ts_web_hosts_ports))
            for host_port in self.args.ts_web_hosts_ports.split(','):
                (host, port) = host_port.rsplit(':', 1)
                # Add this host(RPC IP or Broadcast IP) to ts_cfgs dict. Set web port to access
                # /varz endpoint.
                self.ts_cfgs.setdefault(host, YBTSConfig(self)).set_web_port(port)

        if self.per_region_backup():
            if len(self.args.region) != len(self.args.region_location):
                raise BackupException(
                    "Found {} --region keys and {} --region_location keys. Number of these keys "
                    "must be equal.".format(len(self.args.region), len(self.args.region_location)))

            for i in range(len(self.args.region)):
                self.region_to_location[self.args.region[i]] = self.args.region_location[i]

        live_tservers = self.get_live_tservers()
        if not self.args.disable_checksums:
            if live_tservers:
                # Need to check the architecture for only first node, rest
                # will be same in the cluster.
                xxhash_tool_path = XXH64HASH_TOOL_PATH_K8S if self.is_k8s() else XXH64HASH_TOOL_PATH
                tserver = live_tservers[0]
                try:
                    self.run_ssh_cmd("[ -d '{}' ]".format(xxhash_tool_path),
                                     tserver, upload_cloud_cfg=False).strip()
                    node_machine_arch = self.run_ssh_cmd(['uname', '-m'], tserver,
                                                         upload_cloud_cfg=False).strip()
                    if node_machine_arch and 'x86' not in node_machine_arch:
                        xxh64_bin = XXH64_AARCH_BIN
                    else:
                        xxh64_bin = XXH64_X86_BIN
                    self.xxhash_checksum_path = os.path.join(xxhash_tool_path, xxh64_bin)
                except Exception:
                    logging.warning("[app] xxhsum tool missing on the host, continuing with sha256")
            else:
                raise BackupException("No Live TServer exists. "
                                      "Check the TServer nodes status & try again.")

        if self.args.mac:
            # As this arg is used only for the purpose of tests & we use hardcoded paths only
            # defaulting it to shasum.
            global SHA_TOOL_PATH
            SHA_TOOL_PATH = '/usr/bin/shasum'

    def table_names_str(self, delimeter='.', space=' '):
        return get_table_names_str(self.args.keyspace, self.args.table, delimeter, space)

    def per_region_backup(self):
        return self.args.region_location is not None

    @staticmethod
    def get_snapshot_location(location, bucket):
        if bucket is None:
            return location
        else:
            return os.path.join(location, bucket)

    def snapshot_location(self, bucket, region=None):
        if region is not None:
            if not self.per_region_backup():
                raise BackupException(
                    "Requested region location for non-geo-partitioned backup. Region: {}. "
                    "Locations: {}.".format(region, self.args.region_location))

            if region in self.region_to_location:
                return self.get_snapshot_location(self.region_to_location[region], bucket)

        # Default location.
        return self.get_snapshot_location(self.args.backup_location, bucket)

    def is_s3(self):
        return self.args.storage_type == S3BackupStorage.storage_type()

    def is_gcs(self):
        return self.args.storage_type == GcsBackupStorage.storage_type()

    def is_az(self):
        return self.args.storage_type == AzBackupStorage.storage_type()

    def is_nfs(self):
        return self.args.storage_type == NfsBackupStorage.storage_type()

    def is_k8s(self):
        return self.args.k8s_config is not None

    def is_cloud(self):
        return self.args.storage_type != NfsBackupStorage.storage_type()

    def has_cfg_file(self):
        return self.args.storage_type in [
            GcsBackupStorage.storage_type(), S3BackupStorage.storage_type()]

    def is_ysql_keyspace(self):
        return self.args.keyspace and keyspace_type(self.args.keyspace[0]) == 'ysql'

    def needs_change_user(self):
        return self.args.ssh_user != self.args.remote_user

    def get_main_host_ip(self):
        if self.args.useTserver:
            return self.get_live_tserver_ip()
        else:
            return self.get_leader_master_ip()

    def get_leader_master_ip(self):
        if not self.leader_master_ip:
            all_masters = self.args.masters.split(",")
            # Use first Master's ip in list to get list of all masters.
            self.leader_master_ip = all_masters[0].rsplit(':', 1)[0]

            # Get LEADER ip, if it's ALIVE, else any alive master ip.
            output = self.run_yb_admin(['list_all_masters'])
            for line in output.splitlines():
                if LEADING_UUID_RE.match(line):
                    fields = split_by_tab(line)
                    (rpc_ip_port, state, role) = (fields[1], fields[2], fields[3])
                    broadcast_ip_port = fields[4] if len(fields) > 4 else 'N/A'
                    if state == 'ALIVE':
                        broadcast_ip = self.populate_ip_maps(rpc_ip_port, broadcast_ip_port)
                        alive_master_ip = broadcast_ip
                    if role == 'LEADER':
                        break
            self.leader_master_ip = alive_master_ip

        return self.leader_master_ip

    def get_live_tservers(self):
        """
        Fetches all live tserver-ips, i.e. tservers with state 'ALIVE'.
        :return: A list of tserver_ips: List of server_broadcast_address for the
            live tservers or Rpc address if server_broadcast_address is not set.
        """
        num_loops = 0
        while num_loops < LIVE_TS_SEARCHING_LOOP_MAX_RETRIES:
            logging.info('[app] Start searching for live TServer (try {})'.format(num_loops))
            num_loops += 1
            tserver_ips = []
            output = self.run_yb_admin(['list_all_tablet_servers'])
            for line in output.splitlines():
                if LEADING_UUID_RE.match(line):
                    fields = split_by_space(line)
                    (rpc_ip_port, state) = (fields[1], fields[3])
                    broadcast_ip_port = fields[14] if len(fields) > 14 else 'N/A'
                    if state == 'ALIVE':
                        broadcast_ip = self.populate_ip_maps(rpc_ip_port, broadcast_ip_port)
                        tserver_ips.append(broadcast_ip)

            if tserver_ips:
                return tserver_ips

            logging.info("Sleep for {} seconds before the next live TServer searching round.".
                         format(SLEEP_IN_LIVE_TS_SEARCHING_ROUND_SEC))
            time.sleep(SLEEP_IN_LIVE_TS_SEARCHING_ROUND_SEC)

        raise BackupException(
            "Exceeded max number of retries for the live TServer searching loop ({})!".
            format(LIVE_TS_SEARCHING_LOOP_MAX_RETRIES))

    def get_live_tserver_ip(self):
        if not self.live_tserver_ip:
            alive_ts_ips = self.get_live_tservers()
            if alive_ts_ips:
                leader_master = self.get_leader_master_ip()
                master_hosts = {hp.rsplit(':', 1)[0] for hp in self.args.masters.split(",")}
                # Exclude the Master Leader because the host has maximum network pressure.
                # Let's try to use a follower Master node instead.
                master_hosts.discard(leader_master)
                selected_ts_ips = master_hosts.intersection(alive_ts_ips)
                if not selected_ts_ips:
                    # Try the Master Leader if all Master followers are excluded.
                    selected_ts_ips = {leader_master}.intersection(alive_ts_ips)

                # For rebalancing the user usually adds/removes a TS node. That's why we prefer
                # to use a Master node to prevent selecting of a just ADDED OR a just REMOVED TS.
                # Return the first alive TS if the IP is in the list of Master IPs.
                # Else return just the first alive TS.
                self.live_tserver_ip =\
                    selected_ts_ips.pop() if selected_ts_ips else alive_ts_ips[0]

                if self.args.verbose:
                    logging.info("Selecting alive TS {} from {}".format(
                        self.live_tserver_ip, alive_ts_ips))
            else:
                raise BackupException("Cannot get alive TS: {}".format(alive_ts_ips))

        if not self.live_tserver_ip:
            raise BackupException("No alive TS: {}".format(self.live_tserver_ip))

        return self.live_tserver_ip

    def get_ysql_ip(self):
        if not self.ysql_ip:
            output = ""
            if self.args.ysql_enable_auth:
                # Note that this requires YSQL commands to be run on the master leader.
                # In case of k8s, we get live tserver, since master pod does not have
                # pgsql unix socket.
                socket_fds = self.run_ssh_cmd(
                    "ls /tmp/.yb.*/.s.PGSQL.*", self.get_main_host_ip()).strip().split()
                if len(socket_fds):
                    self.ysql_ip = os.path.dirname(socket_fds[0])
                else:
                    output = "Failed to find local socket."
            elif self.args.ysql_host:
                self.ysql_ip = self.args.ysql_host
            else:
                # Get first ALIVE TS.
                self.ysql_ip = self.get_live_tserver_ip()

            if not self.ysql_ip:
                raise BackupException("Cannot get alive TS:\n{}".format(output))

        return self.ysql_ip

    def run_tool(self, local_tool, remote_tool, std_args, cmd_line_args, run_ip=None, env_vars={}):
        """
        Runs the utility from the configured location.
        :param cmd_line_args: command-line arguments to the tool
        :return: the standard output of the tool
        """

        # Use local tool if it's specified.
        if local_tool:
            if not os.path.exists(local_tool):
                raise BackupException("Tool binary not found at {}".format(local_tool))

            return self.run_program([local_tool] + std_args + cmd_line_args,
                                    env=env_vars, num_retry=10)
        else:
            if run_ip:
                run_at_location = run_ip
            else:
                run_at_location = self.get_leader_master_ip()
            # Using remote tool binary on leader master server.
            return self.run_ssh_cmd(
                [remote_tool] + std_args + cmd_line_args,
                run_at_location,
                num_ssh_retry=10,
                env_vars=env_vars)

    def get_master_addresses_for_servers(self):

        def get_key(val):
            for key, value in self.secondary_to_primary_ip_map.items():
                if val == value:
                    return key

        master_addresses = self.args.masters
        if self.args.local_yb_admin_binary:
            # We are using the local yb-admin, so we should use the management addresses
            return master_addresses

        if self.secondary_to_primary_ip_map:
            master_list_for_servers = list()
            master_address_list = master_addresses.split(',')
            for master in master_address_list:
                master_host, master_port = master.split(":")
                master_for_server = get_key(master_host)
                master_for_server = master_for_server + ":" + master_port
                master_list_for_servers.append(master_for_server)
            master_addresses = ','.join(master_list_for_servers)
        return master_addresses

    def run_yb_admin(self, cmd_line_args, run_ip=None):
        """
        Runs the yb-admin utility from the configured location.
        :param cmd_line_args: command-line arguments to yb-admin
        :return: the standard output of yb-admin
        """

        # Convert to list, since some callers like SequencedParallelCmd will send in tuples.
        cmd_line_args = list(cmd_line_args)
        cmd_line_args.extend(["--never_fsync=" + str(self.args.TEST_never_fsync).lower()])
        # Specify cert file in case TLS is enabled.
        cert_flag = []
        if self.args.certs_dir:
            cert_flag = ["--certs_dir_name", self.args.certs_dir]
            cmd_line_args = cert_flag + cmd_line_args

        master_addresses = self.get_master_addresses_for_servers()

        try:
            return self.run_tool(self.args.local_yb_admin_binary, self.args.remote_yb_admin_binary,
                                 ['--master_addresses', master_addresses],
                                 cmd_line_args, run_ip=run_ip)
        except Exception as ex:
            if "Invalid operation" in str(ex.output.decode('utf-8')):
                raise YbAdminOpNotSupportedException("yb-admin does not support command "
                                                     "{}".format(cmd_line_args))
            raise ex

    def get_ysql_dump_std_args(self):
        args = ['--host=' + self.get_ysql_ip()]
        if self.args.ysql_port:
            args += ['--port=' + self.args.ysql_port]
        return args

    def run_cli_tool(self, cli_tool_with_args):
        """
        Runs a command line tool.
        :param cli_tool_with_args: command-line tool with arguments as a single string
        :return: the standard output of the tool
        """

        run_at_ip = self.get_live_tserver_ip() if self.args.useTserver else None
        return self.run_tool(None, cli_tool_with_args, [], [], run_ip=run_at_ip)

    def run_dump_tool(self, local_tool_binary, remote_tool_binary, cmd_line_args):
        """
        Runs the ysql_dump/ysql_dumpall utility from the configured location.
        :param cmd_line_args: command-line arguments to the tool
        :return: the standard output of the tool
        """

        certs_env = {}
        if self.args.certs_dir:
            certs_env = {
                'FLAGS_certs_dir': self.args.certs_dir,
                'FLAGS_use_node_to_node_encryption': 'true',
                'FLAGS_use_node_hostname_for_local_tserver': 'true',
            }

        run_at_ip = self.get_live_tserver_ip() if self.args.useTserver else None
        # If --ysql_enable_auth is passed, connect with ysql through the remote socket.
        local_binary = None if self.args.ysql_enable_auth else local_tool_binary

        master_addresses = self.get_master_addresses_for_servers()

        return self.run_tool(local_binary, remote_tool_binary,
                             # Latest tools do not need '--masters', but keep it for backward
                             # compatibility with older YB releases.
                             self.get_ysql_dump_std_args() + ['--masters=' + master_addresses],
                             cmd_line_args, run_ip=run_at_ip, env_vars=certs_env)

    def run_ysql_dump(self, cmd_line_args):
        return self.run_dump_tool(self.args.local_ysql_dump_binary,
                                  self.args.remote_ysql_dump_binary, cmd_line_args)

    def run_ysql_dumpall(self, cmd_line_args):
        return self.run_dump_tool(self.args.local_ysql_dumpall_binary,
                                  self.args.remote_ysql_dumpall_binary, cmd_line_args)

    def run_ysql_shell(self, cmd_line_args, db_name="template1"):
        """
        Runs the ysql shell utility from the configured location.
        :param cmd_line_args: command-line arguments to ysql shell
        :return: the standard output of ysql shell
        """
        run_at_ip = None
        if self.args.useTserver:
            run_at_ip = self.get_live_tserver_ip()

        return self.run_tool(
            self.args.local_ysql_shell_binary,
            self.args.remote_ysql_shell_binary,
            # Passing dbname template1 explicitly as ysqlsh fails to connect if
            # yugabyte database is deleted. We assume template1 will always be there
            # in ysqlsh.
            self.get_ysql_dump_std_args() + ['--dbname={}'.format(db_name)],
            cmd_line_args,
            run_ip=run_at_ip)

    def calc_size_in_bytes(self, snapshot_bucket):
        """
        Fetches the backup object size by making a call to respective data source.
        :param snapshot_bucket: the bucket directory under which data directories were uploaded
        :return: backup size in bytes
        """
        # List of unique file paths on which backup was uploaded.
        filepath_list = {self.snapshot_location(snapshot_bucket)}
        if self.per_region_backup():
            for region in self.args.region:
                regional_filepath = self.snapshot_location(snapshot_bucket, region)
                filepath_list.add(regional_filepath)

        # Calculating backup size in bytes on both default and regional backup location.
        backup_size = 0
        for filepath in filepath_list:
            backup_size_cmd = self.storage.backup_obj_size_cmd(filepath)
            try:
                resp = self.run_ssh_cmd(backup_size_cmd, self.get_main_host_ip())
                # if using IMDSv2, s3cmd can return WARNINGs so check each line and ignore
                for line in resp.splitlines():
                    if 'WARNING' not in line:
                        backup_size += int(resp.strip().split()[0])
            except Exception as ex:
                logging.error(
                    'Failed to get backup size, cmd: {}, exception: {}'.format(backup_size_cmd, ex))

        logging.info('Backup size in bytes: {}'.format(backup_size))
        return backup_size

    def create_snapshot(self):
        """
        Creates a new snapshot of the configured table.
        :return: snapshot id
        """
        if self.args.table:
            yb_admin_args = ['create_snapshot'] + self.table_names_str(' ').split(' ')
        elif self.is_ysql_keyspace():
            yb_admin_args = ['create_database_snapshot', self.args.keyspace[0]]
        else:
            yb_admin_args = ['create_keyspace_snapshot', self.args.keyspace[0]]
            if self.args.skip_indexes:
                yb_admin_args.append("skip_indexes")

        output = self.run_yb_admin(yb_admin_args)
        # Ignores any string before and after the creation string + uuid.
        # \S\s matches every character including newlines.
        matched = STARTED_SNAPSHOT_CREATION_RE.match(output)
        if not matched:
            raise BackupException(
                "Couldn't parse create snapshot output! Expected "
                "'Started snapshot creation: <id>' in the end: {}".format(output))
        snapshot_id = matched.group('uuid')
        if not UUID_ONLY_RE.match(snapshot_id):
            raise BackupException("Did not get a valid snapshot id out of yb-admin output:\n" +
                                  output)
        return snapshot_id

    def wait_for_snapshot(self, snapshot_id, op, timeout_sec, update_table_list,
                          complete_state='COMPLETE'):
        """
        Waits for the given snapshot to finish being created or restored.
        """
        start_time = time.time()
        snapshot_done = False
        snapshot_tables = []
        snapshot_keyspaces = []
        snapshot_table_uuids = []
        failed_state = 'FAILED'

        yb_admin_args = ['list_snapshots']
        if update_table_list:
            yb_admin_args += ['SHOW_DETAILS']

        while time.time() - start_time < timeout_sec and not snapshot_done:
            output = self.run_yb_admin(yb_admin_args)
            # Expected format:
            # Snapshot UUID                         State
            # 0436035d-c4c5-40c6-b45b-19538849b0d9  COMPLETE
            #   {"type":"NAMESPACE","id":"e4c5591446db417f83a52c679de03118","data":{"name":"a",...}}
            #   {"type":"TABLE","id":"d9603c2cab0b48ec807936496ac0e70e","data":{"name":"t2",...}}
            #   {"type":"TABLE","id":"28b5cebe9b0c4cdaa70ce9ceab31b1e5","data":{\
            #       "name":"t2idx","indexed_table_id":"d9603c2cab0b48ec807936496ac0e70e",...}}
            # c1ad61bf-a42b-4bbb-94f9-28516985c2c5  COMPLETE
            #   ...
            keyspaces = {}
            for line in output.splitlines():
                if not snapshot_done:
                    if line.find(snapshot_id) == 0:
                        snapshot_data = line.split()
                        found_snapshot_id = snapshot_data[0]
                        state = snapshot_data[1]
                        if found_snapshot_id == snapshot_id:
                            if state == complete_state:
                                snapshot_done = True
                                if not update_table_list:
                                    break
                            elif state == failed_state:
                                raise BackupException(
                                    'Snapshot id %s, %s failed!' % (snapshot_id, op))
                elif update_table_list:
                    if line[0] != ' ':
                        break
                    loaded_json = json.loads(line)
                    object_type = loaded_json['type']
                    object_id = loaded_json['id']
                    data = loaded_json['data']
                    if object_type == 'NAMESPACE' and object_id not in keyspaces:
                        keyspace_prefix = 'ysql.' \
                            if data['database_type'] == 'YQL_DATABASE_PGSQL' else ''
                        keyspaces[object_id] = keyspace_prefix + data['name']
                    elif object_type == 'TABLE':
                        # If a table is colocated, it will share its backing tablets with other
                        # tables.  To avoid repeated work in getting the list of backing tablets for
                        # all tables just add a single table from each colocation group to the table
                        # list.
                        if (not data.get('colocated', False)
                                or is_parent_table_name(data['name'])):
                            snapshot_keyspaces.append(keyspaces[data['namespace_id']])
                            snapshot_tables.append(data['name'])
                            snapshot_table_uuids.append(object_id)

            if not snapshot_done:
                logging.info('Waiting for snapshot %s to complete...' % (op))
                time.sleep(5)

        if not snapshot_done:
            raise BackupException('Timed out waiting for snapshot!')

        if update_table_list:
            if len(snapshot_tables) == 0:
                raise CompatibilityException("Created snapshot does not have tables.")

            if len(snapshot_keyspaces) != len(snapshot_tables):
                raise BackupException(
                    "In the snapshot found {} keyspaces and {} tables. The numbers must be equal.".
                    format(len(snapshot_keyspaces), len(snapshot_tables)))

            self.args.keyspace = snapshot_keyspaces
            self.args.table = snapshot_tables
            self.args.table_uuid = snapshot_table_uuids
            logging.info('Updated list of processing tables: ' + self.table_names_str())

        logging.info('Snapshot id %s %s completed successfully' % (snapshot_id, op))

    def find_tablet_leaders(self):
        """
        Lists all tablets and their leaders for the table of interest.
        :return: a list of (tablet id, leader host, leader host region) tuples
        """
        assert self.args.table

        num_loops = 0
        while num_loops < LEADERS_SEARCHING_LOOP_MAX_RETRIES:
            logging.info('[app] Start searching for tablet leaders (try {})'.format(num_loops))
            num_loops += 1
            found_bad_ts = False
            tablet_leaders = []

            for i in range(0, len(self.args.table)):
                if self.args.table_uuid:
                    yb_admin_args = ['list_tablets', 'tableid.' + self.args.table_uuid[i], '0']
                else:
                    yb_admin_args = ['list_tablets', self.args.keyspace[i], self.args.table[i], '0']

                output = self.run_yb_admin(yb_admin_args)
                for line in output.splitlines():
                    if LEADING_UUID_RE.match(line):
                        fields = split_by_tab(line)
                        (tablet_id, tablet_leader_host_port) = (fields[0], fields[2])
                        (rpc_host, rpc_port) = tablet_leader_host_port.split(":")
                        assert rpc_host in self.rpc_to_broadcast_map
                        ts_host = self.rpc_to_broadcast_map[rpc_host]
                        need_region = self.per_region_backup()
                        # Get the (TS Host address, YBTSConfig object) using this ts_host.
                        (ts_config_ip, ts_config) = self.get_ts_config_detail(ts_host)
                        if self.secondary_to_primary_ip_map:
                            ts_config_ip = self.secondary_to_primary_ip_map.get(ts_config_ip,
                                                                                ts_config_ip)
                        load_cfg = not ts_config.has_data_dirs() or\
                            (need_region and not ts_config.has_region())
                        if load_cfg:
                            try:
                                # Load config using the TS Web host/port provided in the arguments
                                # to the script.
                                ts_config.load(ts_config_ip, need_region)
                            except Exception as ex:
                                found_bad_ts = True
                                logging.warning("Error in TS {} config loading. Retry tablet "
                                                "leaders searching. Error: {}".format(
                                                    ts_host, str(ex)))
                                break
                            # If ts_config_ip( i.e. the TS Web Host ) is different from ts_host,
                            # add an entry to self.ts_cfgs for ts_host with the same YBTSConfig
                            # object.
                            if ts_host != ts_config_ip:
                                self.ts_cfgs.setdefault(ts_host, ts_config)

                        if need_region:
                            region = ts_config.region()
                            # Show the warning only once after this TS config loading.
                            if load_cfg and region not in self.region_to_location:
                                logging.warning("[app] Cannot find tablet {} location for region "
                                                "{}. Using default location instead.".format(
                                                    tablet_id, region))
                        else:
                            region = None

                        tablet_leaders.append((tablet_id, ts_host, region))

                if found_bad_ts:
                    break

            if not found_bad_ts:
                return tablet_leaders

            logging.info("Sleep for {} seconds before the next tablet leaders searching round.".
                         format(SLEEP_IN_LEADERS_SEARCHING_ROUND_SEC))
            time.sleep(SLEEP_IN_LEADERS_SEARCHING_ROUND_SEC)

        raise BackupException(
            "Exceeded max number of retries for the tablet leaders searching loop ({})!".
            format(LEADERS_SEARCHING_LOOP_MAX_RETRIES))

    def create_remote_tmp_dir(self, server_ip):
        if self.args.verbose:
            logging.info("Creating {} on server {}".format(self.get_tmp_dir(), server_ip))

        atexit.register(self.cleanup_remote_temporary_directory, server_ip, self.get_tmp_dir())

        return self.run_ssh_cmd(['mkdir', '-p', self.get_tmp_dir()],
                                server_ip, upload_cloud_cfg=False)

    def upload_local_file_to_server(self, server_ip, local_file_path, dest_dir):
        if self.args.verbose:
            logging.info("Uploading {} to {} on {}".format(local_file_path, dest_dir, server_ip))

        if dest_dir == self.get_tmp_dir():
            output = self.create_remote_tmp_dir(server_ip)
        else:
            if self.args.verbose:
                logging.info("Creating {} on server {}".format(dest_dir, server_ip))
            output = self.run_ssh_cmd(['mkdir', '-p', dest_dir],
                                      server_ip, upload_cloud_cfg=False)

        output += self.upload_file_from_local(server_ip, local_file_path, dest_dir)

        if self.args.verbose:
            logging.info("Uploading {} to {} on {} done: {}".format(
                local_file_path, dest_dir, server_ip, output))

        return output

    def upload_cloud_config(self, server_ip):
        start_time = datetime.utcfromtimestamp(time.time())
        if self.args.verbose:
            logging.info(f'upload_cloud_config with args {server_ip} started at {start_time}')
        if server_ip not in self.server_ips_with_uploaded_cloud_cfg:
            self.server_ips_with_uploaded_cloud_cfg[server_ip] = self.upload_local_file_to_server(
                server_ip, self.cloud_cfg_file_path, self.get_tmp_dir())
        end_time = datetime.utcfromtimestamp(time.time())
        time_taken = end_time - start_time
        if self.args.verbose:
            logging.info(f'upload_cloud_config with args {server_ip} finished at {end_time}. \
                        Time taken = {time_taken.total_seconds():.6f}s.')

    def upload_file_from_local(self, dest_ip, src, dest):
        output = ''
        ssh_key_flag = '-i'
        if self.args.ssh2_enabled:
            ssh_key_flag = '-K'
        if self.is_k8s():
            k8s_details = KubernetesDetails(dest_ip, self.k8s_pod_addr_to_cfg)
            output += self.run_program([
                'kubectl',
                'cp',
                src,
                '{}/{}:{}'.format(
                    k8s_details.namespace, k8s_details.pod_name, dest),
                '-c',
                k8s_details.container,
                '--no-preserve=true'
            ], env=k8s_details.env_config, num_retry=LOCAL_FILE_MAX_RETRIES)
        elif not self.args.no_ssh:
            ssh_key_path = self.args.ssh_key_path
            if self.ip_to_ssh_key_map:
                ssh_key_path = self.ip_to_ssh_key_map.get(dest_ip, ssh_key_path)
            if self.needs_change_user():
                # TODO: Currently ssh_wrapper_with_sudo.sh will only change users to yugabyte,
                # not args.remote_user.
                ssh_wrapper_path = os.path.join(SCRIPT_DIR, 'ssh_wrapper_with_sudo.sh')
                output += self.run_program(
                    ['scp',
                        '-S', ssh_wrapper_path,
                        '-o', 'StrictHostKeyChecking=no',
                        '-o', 'UserKnownHostsFile=/dev/null',
                        ssh_key_flag, ssh_key_path,
                        '-P', self.args.ssh_port,
                        '-q',
                        src,
                        '%s@%s:%s' % (self.args.ssh_user, dest_ip, dest)],
                    num_retry=LOCAL_FILE_MAX_RETRIES)
            else:
                output += self.run_program(
                    ['scp',
                        '-o', 'StrictHostKeyChecking=no',
                        '-o', 'UserKnownHostsFile=/dev/null',
                        ssh_key_flag, ssh_key_path,
                        '-P', self.args.ssh_port,
                        '-q',
                        src,
                        '%s@%s:%s' % (self.args.ssh_user, dest_ip, dest)],
                    num_retry=LOCAL_FILE_MAX_RETRIES)

        return output

    def download_file_to_local(self, src_ip, src, dest):
        output = ''
        ssh_key_flag = '-i'
        if self.args.ssh2_enabled:
            ssh_key_flag = '-K'
        if self.is_k8s():
            k8s_details = KubernetesDetails(src_ip, self.k8s_pod_addr_to_cfg)
            output += self.run_program([
                'kubectl',
                'cp',
                '{}/{}:{}'.format(
                    k8s_details.namespace, k8s_details.pod_name, src),
                dest,
                '-c',
                k8s_details.container,
                '--no-preserve=true'
            ], env=k8s_details.env_config, num_retry=LOCAL_FILE_MAX_RETRIES)
        elif not self.args.no_ssh:
            ssh_key_path = self.args.ssh_key_path
            if self.ip_to_ssh_key_map:
                ssh_key_path = self.ip_to_ssh_key_map.get(src_ip, ssh_key_path)
            if self.needs_change_user():
                # TODO: Currently ssh_wrapper_with_sudo.sh will only change users to yugabyte,
                # not args.remote_user.
                ssh_wrapper_path = os.path.join(SCRIPT_DIR, 'ssh_wrapper_with_sudo.sh')
                output += self.run_program(
                    ['scp',
                        '-S', ssh_wrapper_path,
                        '-o', 'StrictHostKeyChecking=no',
                        '-o', 'UserKnownHostsFile=/dev/null',
                        ssh_key_flag, ssh_key_path,
                        '-P', self.args.ssh_port,
                        '-q',
                        '%s@%s:%s' % (self.args.ssh_user, src_ip, src),
                        dest], num_retry=LOCAL_FILE_MAX_RETRIES)
            else:
                output += self.run_program(
                    ['scp',
                        '-o', 'StrictHostKeyChecking=no',
                        '-o', 'UserKnownHostsFile=/dev/null',
                        ssh_key_flag, ssh_key_path,
                        '-P', self.args.ssh_port,
                        '-q',
                        '%s@%s:%s' % (self.args.ssh_user, src_ip, src),
                        dest], num_retry=LOCAL_FILE_MAX_RETRIES)

        return output

    def run_ssh_cmd(self, cmd, server_ip, upload_cloud_cfg=True, num_ssh_retry=3, env_vars={}):
        """
        Runs the given command on the given remote server over SSH.
        :param cmd: either a string, or a list of arguments. In the latter case, each argument
                    is properly escaped before being passed to ssh.
        :param server_ip: IP address or host name of the server to SSH into.
        :return: the standard output of the SSH command
        """
        if upload_cloud_cfg and self.has_cfg_file():
            self.upload_cloud_config(server_ip)

        if self.args.verbose:
            logging.info("Running command {} on server {}".format(cmd, server_ip))

        if not isinstance(cmd, str):
            if len(cmd) == 1:
                cmd = cmd[0]
            else:
                cmd = quote_cmd_line_for_bash(cmd)

        num_retries = CLOUD_CMD_MAX_RETRIES if self.is_cloud() else num_ssh_retry

        if env_vars:
            # Add env vars to the front of the cmd shell-style like "FOO=bar ls -l"
            bash_env_args = " ".join(["{}={}".format(env_name, pipes.quote(env_val)) for
                                     (env_name, env_val) in env_vars.items()])
            cmd = "{} {}".format(bash_env_args, cmd)

        if self.is_k8s():
            k8s_details = KubernetesDetails(server_ip, self.k8s_pod_addr_to_cfg)
            return self.run_program([
                'kubectl',
                'exec',
                '-t',
                '-n={}'.format(k8s_details.namespace),
                # For k8s, pick the first qualified name, if given a CNAME.
                k8s_details.pod_name,
                '-c',
                k8s_details.container,
                '--',
                'bash',
                '-c',
                cmd],
                num_retry=num_retries,
                env=k8s_details.env_config,
                redirect_stderr=subprocess.PIPE)
        elif not self.args.no_ssh:
            ssh_key_path = self.args.ssh_key_path
            if self.ip_to_ssh_key_map:
                ssh_key_path = self.ip_to_ssh_key_map.get(server_ip, ssh_key_path)
            change_user_cmd = 'sudo -u %s' % (self.args.remote_user) \
                if self.needs_change_user() else ''
            ssh_key_flag = '-K' if self.args.ssh2_enabled else '-i'
            ssh_only_args = [
                '-o', 'UserKnownHostsFile=/dev/null',
                # Control flags here are for ssh multiplexing (reuse the same ssh connections).
                '-o', 'ControlMaster=auto',
                '-o', 'ControlPath={}/ssh-%r@%h:%p'.format(self.get_tmp_dir()),
                '-o', 'ControlPersist=1m',
            ] if not self.args.ssh2_enabled else []
            ssh_command = [
                'ssh',
                '-o', 'StrictHostKeyChecking=no'
            ]
            ssh_command.extend(ssh_only_args)
            ssh_command.extend([
                ssh_key_flag, ssh_key_path,
                '-p', self.args.ssh_port,
                '-q',
                '%s@%s' % (self.args.ssh_user, server_ip),
                'cd / && %s bash -c ' % (change_user_cmd) + pipes.quote(cmd)
            ])
            return self.run_program(ssh_command, num_retry=num_retries)
        else:
            return self.run_program(['bash', '-c', cmd])

    def join_ssh_cmds(self, list_of_arg_tuples, handle):
        (tablet_id, tserver_ip) = handle
        # A list of commands: execute all of the tuples in a single control connection.
        joined_cmd = 'set -ex;'  # Exit as soon as one command fails.
        for args_tuple in list_of_arg_tuples:
            assert isinstance(args_tuple, tuple)
            for args in args_tuple:
                if (isinstance(args, tuple)):
                    joined_cmd += ' '.join(args)
                else:
                    joined_cmd += ' ' + args
            joined_cmd += ';'

        # Return a single arg tuple with the entire joined command.
        # Convert to string to handle python2 converting to 'unicode' by default.
        return [(str(joined_cmd), tserver_ip)]

    def populate_ip_maps(self, rpc_ip_port, broadcast_ip_port):
        """
        Populate the 'broadcast_to_rpc_map' and the 'rpc_to_broadcast_map' dicts.
        If broadcast_ip is available use that, otherwise simply use the rpc_ip for
        the mapping. Additionally, if secondary_to_primary_ip_map is available,
        fetch the entry for given rpc_ip and use that for broadcast_ip.
        :param rpc_ip_port: A string of <rpc_ip>:<rpc_port>.
        :param broadcast_ip_port: A string of <broadcast_ip:port> if available else 'N/A'.
        :return: The obtained broadcast_ip.
        """
        resolved_ip_port = broadcast_ip_port \
            if broadcast_ip_port != 'N/A' and self.args.use_server_broadcast_address \
            else rpc_ip_port
        (broadcast_ip, port) = resolved_ip_port.rsplit(':', 1)
        (rpc_ip, rpc_port) = rpc_ip_port.rsplit(':', 1)
        if self.secondary_to_primary_ip_map:
            broadcast_ip = self.secondary_to_primary_ip_map.get(rpc_ip, broadcast_ip)

        self.broadcast_to_rpc_map[broadcast_ip] = rpc_ip
        self.rpc_to_broadcast_map[rpc_ip] = broadcast_ip
        return broadcast_ip

    def get_ts_config_detail(self, broadcast_ip):
        """
        In the post_process_arguments, we populate the 'ts_cfgs' dict, with the
        provided TS Web host/port. The TS Web host provided can either be Broadcast IP or the
        RPC IP of the tserver. This method aism to return that TS Web Host along with
        the YBTSConfig object.
        :param broadcast_ip: The broadcast_ip of tablet server.
        :return: A tuple of (ts_config_ip, ts_config) where ts_config_ip is the Host address
        for which the 'ts_cfgs' was set during post_process_arguments step. This could either be
        RPC or Broadcast address, depending on arguments supplied to the script, and is required
        to make curl call to the TS /varz endpoint. The ts_config is the
        YBTSConfig object associated with it.
        """
        ts_config_ip = broadcast_ip
        # Ok to be None here.
        ts_config = self.ts_cfgs.get(broadcast_ip)
        rpc_ip = self.broadcast_to_rpc_map[broadcast_ip]
        if rpc_ip != broadcast_ip:
            if broadcast_ip not in self.ts_cfgs:
                ts_config_ip = rpc_ip
                ts_config = self.ts_cfgs.setdefault(rpc_ip, YBTSConfig(self))
        else:
            ts_config = self.ts_cfgs.setdefault(broadcast_ip, YBTSConfig(self))
        return (ts_config_ip, ts_config)

    def find_data_dirs(self, tserver_ip):
        """
        Finds the data directories on the given tserver. This queries the /varz?raw=1 endpoint of
        tserver and extracts --fs_data_dirs flag from response.
        :param tserver_ip: tablet server ip
        :return: a list of top-level YB data directories
        """
        start_time = datetime.utcfromtimestamp(time.time())
        if self.args.verbose:
            logging.info(f'find_data_dirs with args {tserver_ip} started at {start_time}')
        (ts_config_ip, ts_config) = self.get_ts_config_detail(tserver_ip)
        if self.secondary_to_primary_ip_map:
            ts_config_ip = self.secondary_to_primary_ip_map.get(ts_config_ip,
                                                                ts_config_ip)
        if not ts_config.has_data_dirs():
            try:
                ts_config.load(ts_config_ip)
            except Exception as ex:
                logging.warning("Error in TS {} config loading. Skip TS in this "
                                "downloading round. Error: {}".format(tserver_ip, str(ex)))
                return None
        # If ts_config_ip( i.e. the TS Web Host ) is different from tserver_ip, add an
        # entry to self.ts_cfgs for tserver_ip with the same YBTSConfig object.
        if tserver_ip != ts_config_ip:
            self.ts_cfgs.setdefault(tserver_ip, ts_config)
        end_time = datetime.utcfromtimestamp(time.time())
        time_taken = end_time - start_time
        if self.args.verbose:
            logging.info(f'find_data_dirs with args {tserver_ip} finished at {end_time}. \
                     Time taken = {time_taken.total_seconds():.6f}s.')
        return ts_config.data_dirs()

    def generate_snapshot_dirs(self, data_dir_by_tserver, snapshot_id,
                               tablets_by_tserver_ip, table_ids):
        """
        Generate snapshot directories under the given data directory for the given snapshot id
        on the given tservers.
        :param data_dir_by_tserver: data directory on tservers
        :param snapshot_id: snapshot UUID
        :param tablets_by_tserver_ip: a map from tserver ip address to all tablets of our table
            that it is responsible for.
        :param table_ids: new table UUIDs for all tables
        :return: a three-level map: tablet server ip address to a tablet id to all snapshot
            directories for that tablet id that we found.
        """
        tserver_ip_to_tablet_id_to_snapshot_dirs = {}
        deleted_tablets_by_tserver_ip = {}

        tserver_ip_to_tablet_dirs = {}
        for tserver_ip in tablets_by_tserver_ip:
            tserver_ip_to_tablet_dirs.setdefault(tserver_ip, [])

        for table_id in table_ids:
            for tserver_ip in tablets_by_tserver_ip:
                data_dirs = data_dir_by_tserver[tserver_ip]
                # In case of TS config loading error 'data_dirs' is None.
                if data_dirs is None:
                    logging.warning("No data directories on tablet "
                                    "server '{}'.".format(tserver_ip))
                    continue

                tablet_dirs = tserver_ip_to_tablet_dirs[tserver_ip]

                for data_dir in data_dirs:
                    ts_data_dir = data_dir
                    rocksdb_data_dir = strip_dir(ts_data_dir) + ROCKSDB_PATH_PREFIX
                    # Find all tablets for this table on this TS in this data_dir:
                    output = self.run_ssh_cmd(
                        ['find', rocksdb_data_dir] +
                        ([] if self.args.mac else ['!', '-readable', '-prune', '-o']) +
                        ['-name', TABLET_MASK,
                         '-and',
                         '-wholename', TABLET_DIR_GLOB.format(table_id),
                         '-print'],
                        tserver_ip)
                    tablet_dirs += [line.strip() for line in output.split("\n") if line.strip()]

                if self.args.verbose:
                    msg = "Found tablet directories for table '{}' on  tablet server '{}': {}"
                    logging.info(msg.format(table_id, tserver_ip, tablet_dirs))

                if not tablet_dirs:
                    logging.warning("No tablet directory found for table '{}' on "
                                    "tablet server '{}'.".format(table_id, tserver_ip))

        for tserver_ip in tablets_by_tserver_ip:
            tablets = tablets_by_tserver_ip[tserver_ip]
            tablet_dirs = tserver_ip_to_tablet_dirs[tserver_ip]
            tablet_id_to_snapshot_dirs = \
                tserver_ip_to_tablet_id_to_snapshot_dirs.setdefault(tserver_ip, {})
            deleted_tablets = deleted_tablets_by_tserver_ip.setdefault(tserver_ip, set())
            tablet_dir_by_id = {}

            for tablet_dir in tablet_dirs:
                tablet_dir_by_id[tablet_dir[-TABLET_UUID_LEN:]] = tablet_dir

            for tablet_id in tablets:
                if tablet_id in tablet_dir_by_id:
                    # Tablet was found in a data dir - use this path.
                    snapshot_dir = tablet_dir_by_id[tablet_id] + '.snapshots/' + snapshot_id
                    tablet_id_to_snapshot_dirs.setdefault(tablet_id, set()).add(snapshot_dir)
                else:
                    # Tablet was not found. That means that the tablet was deleted from this TS.
                    # Let's ignore the tablet and allow retry-loop to find and process new
                    # tablet location on the next downloading round.
                    # Second case: the TS config is not available (so data directories are not
                    # known). Retry TS config downloading on the next downloading round.
                    deleted_tablets.add(tablet_id)
                    if self.args.verbose:
                        logging.info("Tablet '{}' directory was not found on "
                                     "tablet server '{}'.".format(tablet_id, tserver_ip))

            if self.args.verbose:
                logging.info("Downloading list for tablet server '{}': {}".format(
                    tserver_ip, tablet_id_to_snapshot_dirs))

            if deleted_tablets:
                logging.info("No snapshot directories generated on tablet server '{}' "
                             "for tablet ids: '{}'".format(tserver_ip, deleted_tablets))

        return (tserver_ip_to_tablet_id_to_snapshot_dirs, deleted_tablets_by_tserver_ip)

    def find_snapshot_directories(self, data_dir, snapshot_id, tserver_ip):
        """
        Find snapshot directories under the given data directory for the given snapshot id on the
        given tserver.
        :param data_dir: top-level data directory
        :param snapshot_id: snapshot UUID
        :param tserver_ip: tablet server IP or host name
        :return: a list of absolute paths of remote snapshot directories for the given snapshot
        """
        start_time = datetime.utcfromtimestamp(time.time())
        if self.args.verbose:
            logging.info(
                f'find_snapshot_directories with args {data_dir, snapshot_id, tserver_ip} \
                started at {start_time}')
        output = self.run_ssh_cmd(
            ['find', data_dir] +
            ([] if self.args.mac else ['!', '-readable', '-prune', '-o']) +
            ['-name', snapshot_id, '-and',
             '-wholename', SNAPSHOT_DIR_GLOB,
             '-print'],
            tserver_ip)
        end_time = datetime.utcfromtimestamp(time.time())
        time_taken = end_time - start_time
        if self.args.verbose:
            logging.info(
                f'find_snapshot_directories with args {data_dir, snapshot_id, tserver_ip} \
                finished at {end_time}. \
                Time taken = {time_taken.total_seconds():.6f}s.')
        return [line.strip() for line in output.split("\n") if line.strip()]

    def upload_snapshot_directories(self, tablet_leaders, snapshot_id, snapshot_bucket):
        """
        Uploads snapshot directories from all tablet servers hosting our table to subdirectories
        of the given target backup directory.
        :param tablet_leaders: a list of (tablet_id, tserver_ip, tserver_region) tuples
        :param snapshot_id: self-explanatory
        :param snapshot_bucket: the bucket directory under which to upload the data directories
        """
        with terminating(ThreadPool(self.args.parallelism)) as pool:
            self.pools.append(pool)

            tablets_by_leader_ip = {}
            location_by_tablet = {}
            for (tablet_id, leader_ip, tserver_region) in tablet_leaders:
                tablets_by_leader_ip.setdefault(leader_ip, set()).add(tablet_id)
                location_by_tablet[tablet_id] = self.snapshot_location(snapshot_bucket,
                                                                       tserver_region)

            tserver_ips = sorted(tablets_by_leader_ip.keys())
            data_dir_by_tserver = {}
            for tserver_ip in tserver_ips:
                data_dir_by_tserver[tserver_ip] = copy.deepcopy(
                    # Data dirs must be loaded by the moment in find_tablet_leaders().
                    self.ts_cfgs[tserver_ip].data_dirs())

            # Upload config to every TS here to prevent parallel uploading of the config
            # in 'find_snapshot_directories' below.
            if self.has_cfg_file():
                SingleArgParallelCmd(self.upload_cloud_config,
                                     tserver_ips, verbose=self.args.verbose).run(pool)

            parallel_find_snapshots = MultiArgParallelCmd(self.find_snapshot_directories,
                                                          verbose=self.args.verbose)
            tservers_processed = []
            while len(tserver_ips) > len(tservers_processed):
                for tserver_ip in list(tserver_ips):
                    if tserver_ip not in tservers_processed:
                        data_dirs = data_dir_by_tserver[tserver_ip]
                        if len(data_dirs) > 0:
                            ts_data_dir = data_dir = data_dirs[0]
                            rocksdb_data_dir = strip_dir(ts_data_dir) + ROCKSDB_PATH_PREFIX
                            parallel_find_snapshots.add_args(rocksdb_data_dir,
                                                             snapshot_id, tserver_ip)
                            data_dirs.remove(data_dir)

                            if len(data_dirs) == 0:
                                tservers_processed += [tserver_ip]
                        else:
                            tservers_processed += [tserver_ip]

            find_snapshot_dir_results = parallel_find_snapshots.run(pool)

            leader_ip_to_tablet_id_to_snapshot_dirs = self.rearrange_snapshot_dirs(
                find_snapshot_dir_results, snapshot_id, tablets_by_leader_ip)

            if (self.args.TEST_sleep_after_find_snapshot_dirs):
                logging.info("Sleeping to allow a tablet split to take place and delete snapshot "
                             "dirs.")
                time.sleep(TEST_SLEEP_AFTER_FIND_SNAPSHOT_DIRS_SEC)

            if self.args.TEST_drop_table_before_upload:
                logging.info("Dropping table mytbl before uploading snapshot")
                db_name = keyspace_name(self.args.keyspace[0])
                drop_table_cmd = ['--command=drop table mytbl']
                self.run_ysql_shell(drop_table_cmd, db_name)

            parallel_uploads = SequencedParallelCmd(
                self.run_ssh_cmd, preprocess_args_fn=self.join_ssh_cmds, verbose=self.args.verbose)
            self.prepare_cloud_ssh_cmds(
                parallel_uploads, leader_ip_to_tablet_id_to_snapshot_dirs, location_by_tablet,
                snapshot_id, tablets_by_leader_ip, upload=True, snapshot_metadata=None)

            # Run a sequence of steps for each tablet, handling different tablets in parallel.
            parallel_uploads.run(pool)

    def rearrange_snapshot_dirs(
            self, find_snapshot_dir_results, snapshot_id, tablets_by_tserver_ip):
        """
        :param find_snapshot_dir_results: a map from (data_dir, snapshot_id, tserver_ip)
            tuples to the list of snapshot directories under that data directory on that tserver.
            (snapshot_id here is always the single snapshot_id we're dealing with.)
        :param snapshot_id: the snapshot id!
        :param tablets_by_tserver_ip: a map from tserver ip address to all tablets of our table
            that it is responsible for.
        :return: a three-level map: tablet server ip address to a tablet id to all snapshot
            directories for that tablet id that we found.
        """
        tserver_ip_to_tablet_id_to_snapshot_dirs = {}
        for key in find_snapshot_dir_results:
            (data_dir, snapshot_id_unused, tserver_ip) = key
            snapshot_dirs = find_snapshot_dir_results[key]
            assert snapshot_id_unused == snapshot_id
            tablet_id_to_snapshot_dirs =\
                tserver_ip_to_tablet_id_to_snapshot_dirs.setdefault(tserver_ip, {})

            for snapshot_dir in snapshot_dirs:
                suffix_match = SNAPSHOT_DIR_SUFFIX_RE.match(snapshot_dir)
                if not suffix_match:
                    logging.warning(
                        ("Could not parse tablet id and snapshot id out of snapshot "
                         "directory: '{}'").format(snapshot_dir))
                    continue

                if snapshot_id != suffix_match.group(2):
                    raise BackupException(
                        "Snapshot directory does not end with snapshot id: '{}'".format(
                            snapshot_dir))

                tablet_id = suffix_match.group(1)
                # During CREATE BACKUP only the LEADER tablet replicas are needed.
                # So, ignore the following warning for FOLLOWERS. It's expected because
                # FOLLOWERS replicas are not in the 'tablets_by_tserver_ip' list
                # (the list 'tablets_by_tserver_ip' contains only the LEADER replicas).
                if tablet_id not in tablets_by_tserver_ip[tserver_ip]:
                    if self.args.verbose:
                        logging.warning(
                            ("Found a snapshot directory '{}' on tablet server '{}' that is not "
                             "present in the list of tablets we are interested in that have this "
                             "tserver hosting it ({}), skipping.").format(
                                snapshot_dir, tserver_ip,
                                ", ".join(sorted(tablets_by_tserver_ip[tserver_ip]))))
                    continue

                tablet_id_to_snapshot_dirs.setdefault(tablet_id, set()).add(snapshot_dir)

        return tserver_ip_to_tablet_id_to_snapshot_dirs

    def create_checksum_cmd_not_quoted(self, file_path, checksum_file_path):
        tool_path = self.xxhash_checksum_path if self.xxhash_checksum_path else SHA_TOOL_PATH
        prefix = pipes.quote(tool_path)
        return "{} {} > {}".format(prefix, file_path, checksum_file_path)

    def create_checksum_cmd(self, file_path, checksum_file_path):
        return self.create_checksum_cmd_not_quoted(
            pipes.quote(file_path), pipes.quote(checksum_file_path))

    def checksum_path(self, file_path):
        ext = XXH64_FILE_EXT if self.xxhash_checksum_path else SHA_FILE_EXT
        return file_path + '.' + ext

    def checksum_path_downloaded(self, file_path):
        return self.checksum_path(file_path) + '.downloaded'

    def create_checksum_cmd_for_dir(self, dir_path):
        return self.create_checksum_cmd_not_quoted(
            os.path.join(pipes.quote(strip_dir(dir_path)), '[!i]*'),
            pipes.quote(self.checksum_path(strip_dir(dir_path))))

    def prepare_upload_command(self, parallel_commands, snapshot_filepath, tablet_id,
                               tserver_ip, snapshot_dir):
        """
        Prepares the command to upload the backup files to backup location from the tservers.

        :param parallel_commands: result parallel commands to run.
        :param snapshot_filepath: Filepath/cloud url where the backup must be stored.
        :param tablet_id: tablet_id for the tablet whose data we would like to upload.
        :param tserver_ip: tserver ip from which the data needs to be uploaded.
        :param snapshot_dir: The snapshot directory on the tserver from which we need to upload.
        """
        target_tablet_filepath = os.path.join(snapshot_filepath, 'tablet-%s' % (tablet_id))
        if not self.args.disable_checksums:
            logging.info('Creating check-sum for %s on tablet server %s' % (
                         snapshot_dir, tserver_ip))
            create_checksum_cmd = self.create_checksum_cmd_for_dir(snapshot_dir)

            target_checksum_filepath = self.checksum_path(target_tablet_filepath)
            snapshot_dir_checksum = self.checksum_path(strip_dir(snapshot_dir))
            logging.info('Uploading %s from tablet server %s to %s URL %s' % (
                         snapshot_dir_checksum, tserver_ip, self.args.storage_type,
                         target_checksum_filepath))
            upload_checksum_cmd = self.storage.upload_file_cmd(
                snapshot_dir_checksum, target_checksum_filepath)
            delete_checksum_cmd = ['rm', snapshot_dir_checksum]

        target_filepath = target_tablet_filepath + '/'
        logging.info('Uploading %s from tablet server %s to %s URL %s' % (
                     snapshot_dir, tserver_ip, self.args.storage_type, target_filepath))
        upload_tablet_cmd = self.storage.upload_dir_cmd(snapshot_dir, target_filepath)

        # Commands to be run on TSes over ssh for uploading the tablet backup.
        if not self.args.disable_checksums:
            # 1. Create check-sum file (via sha256sum tool).
            parallel_commands.add_args(create_checksum_cmd)
            # 2. Upload check-sum file.
            parallel_commands.add_args(tuple(upload_checksum_cmd))
            # 3. Delete local check-sum file.
            parallel_commands.add_args(tuple(delete_checksum_cmd))
        # 4. Upload tablet folder.
        parallel_commands.add_args(tuple(upload_tablet_cmd))

        # Cleanup azcopy logs and plan files
        if self.is_az():
            parallel_commands.add_args(tuple(self.storage.clean_up_logs_cmd()))

    def prepare_download_command(self, parallel_commands, tablet_id,
                                 tserver_ip, snapshot_dir, snapshot_metadata):
        """
        Prepares the command to download the backup files to the tservers.

        :param parallel_commands: result parallel commands to run.
        :param tablet_id: tablet_id for the tablet whose data we would like to download.
        :param tserver_ip: tserver ip from which the data needs to be downloaded.
        :param snapshot_dir: The snapshot directory on the tserver to which we need to download.
        """
        if tablet_id not in snapshot_metadata['tablet']:
            raise BackupException('Could not find metadata for tablet id {}'.format(tablet_id))

        old_tablet_id = snapshot_metadata['tablet'][tablet_id]
        snapshot_filepath = snapshot_metadata['tablet_location'][old_tablet_id]
        source_filepath = os.path.join(snapshot_filepath, 'tablet-%s/' % (old_tablet_id))
        snapshot_dir_tmp = strip_dir(snapshot_dir) + '.tmp/'
        logging.info('Downloading %s from %s to %s on tablet server %s' % (source_filepath,
                     self.args.storage_type, snapshot_dir_tmp, tserver_ip))

        # Download the data to a tmp directory and then move it in place.
        cmd = self.storage.download_dir_cmd(source_filepath, snapshot_dir_tmp)

        source_checksum_filepath = self.checksum_path(
            os.path.join(snapshot_filepath, 'tablet-%s' % (old_tablet_id)))
        snapshot_dir_checksum = self.checksum_path_downloaded(strip_dir(snapshot_dir))
        cmd_checksum = self.storage.download_file_cmd(
            source_checksum_filepath, snapshot_dir_checksum)

        create_checksum_cmd = self.create_checksum_cmd_for_dir(snapshot_dir_tmp)
        # Throw an error on failed checksum comparison, this will trigger this entire command
        # chain to be retried.
        generated_checksum = self.checksum_path(strip_dir(snapshot_dir_tmp))
        check_checksum_cmd = compare_checksums_cmd(
            snapshot_dir_checksum,
            generated_checksum,
            error_on_failure=True)

        rmcmd = ['rm', '-rf', snapshot_dir]
        mkdircmd = ['mkdir', '-p', snapshot_dir_tmp]
        mvcmd = ['mv', snapshot_dir_tmp, snapshot_dir]
        rm_checksum_cmd = ['rm', snapshot_dir_checksum, generated_checksum]

        # Commands to be run over ssh for downloading the tablet backup.
        # 1. Clean-up: delete target tablet folder.
        parallel_commands.add_args(tuple(rmcmd))
        # 2. Create temporary snapshot dir.
        parallel_commands.add_args(tuple(mkdircmd))
        # 3. Download tablet folder.
        parallel_commands.add_args(tuple(cmd))
        if not self.args.disable_checksums:
            # 4. Download check-sum file.
            parallel_commands.add_args(tuple(cmd_checksum))
            # 5. Create new check-sum file.
            parallel_commands.add_args(create_checksum_cmd)
            # 6. Compare check-sum files.
            parallel_commands.add_args(check_checksum_cmd)
            # 7. Remove downloaded and generated check-sum files
            parallel_commands.add_args(tuple(rm_checksum_cmd))
        # 8. Move the backup in place.
        parallel_commands.add_args(tuple(mvcmd))

        # Cleanup azcopy logs and plan files
        if self.is_az():
            parallel_commands.add_args(tuple(self.storage.clean_up_logs_cmd()))

    def prepare_cloud_ssh_cmds(
            self, parallel_commands, tserver_ip_to_tablet_id_to_snapshot_dirs, location_by_tablet,
            snapshot_id, tablets_by_tserver_ip, upload, snapshot_metadata):
        """
        Prepares cloud_command-over-ssh command lines for uploading the snapshot.

        :param parallel_commands: result parallel commands to run.
        :param tserver_ip_to_tablet_id_to_snapshot_dirs: the three-level map as returned by
            rearrange_snapshot_dirs.
        :param location_by_tablet: target cloud URL for every tablet to create snapshot
            directories under
        :param snapshot_id: the snapshot id we're dealing with
        :param tablets_by_tserver_ip: a map from tserver ip to all tablet ids that tserver is the
            responsible for.
        :param upload: True if we are uploading files to cloud, false if we are downloading files
            from cloud.
        :param snapshot_metadata: In case of downloading files from cloud to restore a backup,
            this is the snapshot metadata stored in cloud for the backup.
        """
        tserver_ip_to_tablet_ids_with_data_dirs = {}
        for tserver_ip in tserver_ip_to_tablet_id_to_snapshot_dirs:
            tserver_ip_to_tablet_ids_with_data_dirs.setdefault(tserver_ip, set())

        tservers_processed = []
        while len(tserver_ip_to_tablet_id_to_snapshot_dirs) > len(tservers_processed):
            for tserver_ip in list(tserver_ip_to_tablet_id_to_snapshot_dirs):
                if tserver_ip not in tservers_processed:
                    tablet_id_to_snapshot_dirs =\
                        tserver_ip_to_tablet_id_to_snapshot_dirs[tserver_ip]
                    tablet_ids_with_data_dirs = tserver_ip_to_tablet_ids_with_data_dirs[tserver_ip]
                    if len(tablet_id_to_snapshot_dirs) > 0:
                        tablet_id = list(tablet_id_to_snapshot_dirs)[0]
                        snapshot_dirs = tablet_id_to_snapshot_dirs[tablet_id]

                        if len(snapshot_dirs) > 1:
                            raise BackupException(
                                ('Found multiple snapshot directories on tserver {} for snapshot '
                                 'id {}: {}').format(tserver_ip, snapshot_id, snapshot_dirs))

                        assert len(snapshot_dirs) == 1
                        snapshot_dir = list(snapshot_dirs)[0] + '/'
                        # Pass in the tablet_id and tserver_ip, so if we fail then we know on which
                        # tserver and for what tablet we failed on.
                        parallel_commands.start_command((tablet_id, tserver_ip))

                        if upload:
                            self.prepare_upload_command(
                                parallel_commands, location_by_tablet[tablet_id], tablet_id,
                                tserver_ip, snapshot_dir)
                        else:
                            self.prepare_download_command(
                                parallel_commands, tablet_id,
                                tserver_ip, snapshot_dir, snapshot_metadata)

                        tablet_ids_with_data_dirs.add(tablet_id)
                        tablet_id_to_snapshot_dirs.pop(tablet_id)

                        if len(tablet_id_to_snapshot_dirs) == 0:
                            tservers_processed += [tserver_ip]

                            if tablet_ids_with_data_dirs != tablets_by_tserver_ip[tserver_ip]:
                                for possible_tablet_id in tablets_by_tserver_ip[tserver_ip]:
                                    if possible_tablet_id not in tablet_ids_with_data_dirs:
                                        logging.error(
                                            ("No snapshot directory found for tablet id '{}' on "
                                                "tablet server '{}'.").format(
                                                    possible_tablet_id, tserver_ip))
                                raise BackupException("Did not find snapshot directories for some "
                                                      + "tablets on tablet server " + tserver_ip)
                    else:
                        tservers_processed += [tserver_ip]

    def get_tmp_dir(self):
        if not self.tmp_dir_name:
            tmp_dir = '/tmp/yb_backup_' + random_string(16)
            atexit.register(self.cleanup_temporary_directory, tmp_dir)
            self.run_program(['mkdir', '-p', tmp_dir])
            self.tmp_dir_name = tmp_dir

        return self.tmp_dir_name

    def upload_encryption_key_file(self):
        key_file = os.path.basename(self.args.backup_keys_source)
        key_file_dest = os.path.join("/".join(self.args.backup_location.split("/")[:-1]), key_file)
        if self.is_nfs():
            # Upload keys file from local to NFS mount path on DB node.
            self.upload_local_file_to_server(self.get_main_host_ip(),
                                             self.args.backup_keys_source,
                                             os.path.dirname(key_file_dest))
        elif self.is_az():
            self.run_program(self.storage.upload_file_cmd(self.args.backup_keys_source,
                             key_file_dest, True))
        else:
            self.run_program(self.storage.upload_file_cmd(self.args.backup_keys_source,
                             key_file_dest))
        self.run_program(["rm", self.args.backup_keys_source])

    def download_file_from_server(self, server_ip, file_path, dest_dir):
        if self.args.verbose:
            logging.info("Downloading {} to local dir {} from {}".format(
                file_path, dest_dir, server_ip))

        output = self.download_file_to_local(server_ip, file_path, dest_dir)

        if self.args.verbose:
            logging.info("Downloading {} to local dir {} from {} done: {}".format(
                file_path, dest_dir, server_ip, output))

    def download_encryption_key_file(self):
        key_file = os.path.basename(self.args.restore_keys_destination)
        key_file_src = os.path.join("/".join(self.args.backup_location.split("/")[:-1]), key_file)
        if self.is_nfs():
            # Download keys file from NFS mount path on DB node to local.
            self.download_file_from_server(self.get_main_host_ip(),
                                           key_file_src,
                                           self.args.restore_keys_destination)
        elif self.is_az():
            self.run_program(
                self.storage.download_file_cmd(key_file_src, self.args.restore_keys_destination,
                                               True)
            )
        else:
            self.run_program(
                self.storage.download_file_cmd(key_file_src, self.args.restore_keys_destination)
            )

    def delete_bucket_obj(self, backup_path):
        logging.info("[app] Removing backup directory '{}'".format(backup_path))

        del_cmd = self.storage.delete_obj_cmd(backup_path)
        if self.is_nfs():
            self.run_ssh_cmd(' '.join(del_cmd), self.get_leader_master_ip())

            # Backup location of a keyspace is typically of the format
            # univ-<univ_uuid>/backup-<backup_time>/multi-table-<keyspace>
            # If there are 2 keyspaces keyspace1, keyspace2 in a universe, then the locations are
            # univ-<univ_uuid>/backup-<backup_time>/multi-table-keyspace1 and
            # univ-<univ_uuid>/backup-<backup_time>/multi-table-keyspace2.
            # While deleting these backups we are deleting the directories multi-table-keyspace1
            # and multi-table-keyspace2 but not deleting the empty directory backup-<backup_time>.
            # The change here is to delete the backup-<backup_time> directory if empty.
            del_dir_cmd = ["rm", "-df", pipes.quote(re.search('.*(?=/)', backup_path)[0])]
            try:
                self.run_ssh_cmd(' '.join(del_dir_cmd), self.get_leader_master_ip())
            except Exception as ex:
                if "Directory not empty" not in str(ex.output.decode('utf-8')):
                    raise ex

        else:
            self.run_program(del_cmd)

    def find_nfs_storage(self, tserver_ip):
        """
        Finds the NFS storage path mounted on the given tserver.
        if we don't find storage path mounted on given tserver IP we
        raise exception
        :param tserver_ip: tablet server ip
        """
        start_time = datetime.utcfromtimestamp(time.time())
        if self.args.verbose:
            logging.info(f'find_nfs_storage with args {tserver_ip} started at {start_time}')
        try:
            self.run_ssh_cmd(['ls', self.args.nfs_storage_path], tserver_ip)
            end_time = datetime.utcfromtimestamp(time.time())
            time_taken = end_time - start_time
            if self.args.verbose:
                logging.info(f'find_nfs_storage with args {tserver_ip} finished at {end_time}. \
                            Time taken = {time_taken.total_seconds():.6f}s.')
        except Exception as ex:
            raise BackupException(
                ('Did not find nfs backup storage path: %s mounted on tablet server %s'
                 % (self.args.nfs_storage_path, tserver_ip)))

    def upload_metadata_and_checksum(self, src_path, dest_path):
        """
        Upload metadata file and checksum file to the target backup location.
        :param src_path: local metadata file path
        :param dest_path: destination metadata file path
        """
        if not self.args.disable_checksums:
            src_checksum_path = self.checksum_path(src_path)
            dest_checksum_path = self.checksum_path(dest_path)

        if self.args.local_yb_admin_binary:
            if not os.path.exists(src_path):
                raise BackupException(
                    "Could not find metadata file at '{}'".format(src_path))

            if not self.args.disable_checksums:
                logging.info('Creating check-sum for %s' % (src_path))
                self.run_program(
                    self.create_checksum_cmd(src_path, src_checksum_path))

                logging.info('Uploading %s to %s' % (src_checksum_path, dest_checksum_path))
                self.run_program(
                    self.storage.upload_file_cmd(src_checksum_path, dest_checksum_path))

            logging.info('Uploading %s to %s' % (src_path, dest_path))
            self.run_program(
                self.storage.upload_file_cmd(src_path, dest_path))
        else:
            server_ip = self.get_main_host_ip()

            if not self.args.disable_checksums:
                logging.info('Creating check-sum for %s on tablet server %s' % (
                             src_path, server_ip))
                self.run_ssh_cmd(
                    self.create_checksum_cmd(src_path, src_checksum_path),
                    server_ip)

                logging.info('Uploading %s from tablet server %s to %s URL %s' % (
                             src_checksum_path, server_ip,
                             self.args.storage_type, dest_checksum_path))
                self.run_ssh_cmd(
                    self.storage.upload_file_cmd(src_checksum_path, dest_checksum_path),
                    server_ip)

            logging.info('Uploading %s from tablet server %s to %s URL %s' % (
                         src_path, server_ip, self.args.storage_type, dest_path))
            self.run_ssh_cmd(
                self.storage.upload_file_cmd(src_path, dest_path),
                server_ip)

            # Cleanup azure logs and plan files after upload
            if self.is_az():
                self.run_ssh_cmd(self.storage.clean_up_logs_cmd(), server_ip)

    def get_ysql_catalog_version(self):
        """
        Get current YSQL Catalog version.
        :return: YSQL Catalog version
        """
        output = self.run_yb_admin(['ysql_catalog_version'])
        matched = YSQL_CATALOG_VERSION_RE.match(output)
        if not matched:
            raise BackupException(
                "Couldn't parse ysql_catalog_version output! Expected "
                "'Version: <number>' in the end: {}".format(output))
        return matched.group('version')

    def create_metadata_files(self):
        """
        :return: snapshot_id and list of sql_dump files
        """
        if ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR:
            if self.args.use_tablespaces:
                raise BackupException("--use_tablespaces can be used in RESTORE mode only")

        if self.args.use_roles:
            raise BackupException("--use_roles can be used in RESTORE mode only")

        snapshot_id = None
        dump_files = []
        pg_based_backup = self.args.pg_based_backup
        if self.is_ysql_keyspace():
            if ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR:
                backup_tablespaces = self.args.backup_tablespaces
            else:
                backup_tablespaces = self.args.use_tablespaces or self.args.backup_tablespaces

            sql_tbsp_dump_path = os.path.join(self.get_tmp_dir(), SQL_TBSP_DUMP_FILE_NAME)\
                if backup_tablespaces else None
            sql_dump_path = os.path.join(self.get_tmp_dir(), SQL_DUMP_FILE_NAME)
            db_name = keyspace_name(self.args.keyspace[0])
            ysql_dump_args = ['--include-yb-metadata', '--serializable-deferrable', '--create',
                              '--schema-only', '--dbname=' + db_name, '--file=' + sql_dump_path]
            if sql_tbsp_dump_path:
                logging.info("[app] Creating ysql dump for tablespaces to {}".format(
                    sql_tbsp_dump_path))
                self.run_ysql_dumpall(['--tablespaces-only', '--include-yb-metadata',
                                       '--file=' + sql_tbsp_dump_path])
                dump_files.append(sql_tbsp_dump_path)
            else:
                ysql_dump_args.append('--no-tablespaces')

            sql_roles_dump_path = os.path.join(self.get_tmp_dir(), SQL_ROLES_DUMP_FILE_NAME)\
                if self.args.backup_roles else None
            if sql_roles_dump_path:
                logging.info("[app] Creating ysql dump for roles to {}".format(
                    sql_roles_dump_path))
                self.run_ysql_dumpall(['--roles-only', '--include-yb-metadata',
                                       '--file=' + sql_roles_dump_path])
                dump_files.append(sql_roles_dump_path)
            elif ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR:
                ysql_dump_args.append('--no-privileges')

            logging.info("[app] Creating ysql dump for DB '{}' to {}".format(
                db_name, sql_dump_path))
            self.run_ysql_dump(ysql_dump_args)

            dump_files.append(sql_dump_path)
            if pg_based_backup:
                sql_data_dump_path = os.path.join(self.get_tmp_dir(), SQL_DATA_DUMP_FILE_NAME)
                logging.info("[app] Performing ysql_dump based backup!")
                self.run_ysql_dump(
                    ['--include-yb-metadata', '--serializable-deferrable', '--data-only',
                     '--dbname=' + db_name, '--file=' + sql_data_dump_path])
                dump_files.append(sql_data_dump_path)

        if not self.args.snapshot_id and not pg_based_backup:
            snapshot_id = self.create_snapshot()
            logging.info("Snapshot started with id: %s" % snapshot_id)
            # TODO: Remove the following try-catch for compatibility to un-relax the code, after
            #       we ensure nobody uses versions < v2.1.4 (after all move to >= v2.1.8).
            try:
                # With 'update_table_list=True' it runs: 'yb-admin list_snapshots SHOW_DETAILS'
                # to get updated list of backed up namespaces and tables. Note that the last
                # argument 'SHOW_DETAILS' is not supported in old YB versions (< v2.1.4).
                self.wait_for_snapshot(snapshot_id, 'creating', CREATE_SNAPSHOT_TIMEOUT_SEC,
                                       update_table_list=True)
            except CompatibilityException as ex:
                logging.info("Ignoring the exception in the compatibility mode: {}".format(ex))
                # In the compatibility mode repeat the command in old style
                # (without the new command line argument 'SHOW_DETAILS').
                # With 'update_table_list=False' it runs: 'yb-admin list_snapshots'.
                self.wait_for_snapshot(snapshot_id, 'creating', CREATE_SNAPSHOT_TIMEOUT_SEC,
                                       update_table_list=False)

            if not self.args.no_snapshot_deleting:
                logging.info("Snapshot %s will be deleted at exit...", snapshot_id)
                atexit.register(self.delete_created_snapshot, snapshot_id)

        return (snapshot_id, dump_files)

    def create_and_upload_metadata_files(self, snapshot_bucket):
        """
        Generates and uploads metadata files describing the given snapshot to the target
        backup location.
        :param snapshot_bucket: Backup subfolder under which to create a path
        :return: snapshot id
        """
        if self.args.snapshot_id:
            logging.info("Using existing snapshot ID: '{}'".format(self.args.snapshot_id))
            snapshot_id = self.args.snapshot_id

        if self.args.local_yb_admin_binary:
            self.run_program(['mkdir', '-p', self.get_tmp_dir()])
        else:
            self.create_remote_tmp_dir(self.get_main_host_ip())

        is_ysql = self.is_ysql_keyspace()
        if is_ysql:
            start_version = self.get_ysql_catalog_version()

        stored_keyspaces = self.args.keyspace
        stored_tables = self.args.table
        stored_table_uuids = self.args.table_uuid
        num_retry = CREATE_METAFILES_MAX_RETRIES

        while num_retry > 0:
            num_retry = num_retry - 1

            (snapshot_id, dump_files) = self.create_metadata_files()

            if is_ysql:
                final_version = self.get_ysql_catalog_version()
                logging.info('[app] YSQL catalog versions: {} - {}'.format(
                             start_version, final_version))
                if final_version == start_version:
                    break  # Ok. No table schema changes during meta data creating.
                else:
                    # wait_for_snapshot() can update the variables - restore them back.
                    self.args.keyspace = stored_keyspaces
                    self.args.table = stored_tables
                    self.args.table_uuid = stored_table_uuids

                    start_version = final_version
                    logging.info('[app] Retry creating metafiles ({} retries left)'.format(
                                 num_retry))
            else:
                break  # Ok. No need to retry for YCQL.

        if num_retry == 0:
            raise BackupException("Couldn't create metafiles due to catalog changes")

        snapshot_filepath = self.snapshot_location(snapshot_bucket)
        if snapshot_id:
            metadata_path = os.path.join(self.get_tmp_dir(), METADATA_FILE_NAME)
            logging.info('[app] Exporting snapshot {} to {}'.format(snapshot_id, metadata_path))
            self.run_yb_admin(['export_snapshot', snapshot_id, metadata_path],
                              run_ip=self.get_main_host_ip())

            self.upload_metadata_and_checksum(metadata_path,
                                              os.path.join(snapshot_filepath, METADATA_FILE_NAME))
        for file_path in dump_files:
            self.upload_metadata_and_checksum(
                file_path, os.path.join(snapshot_filepath, os.path.basename(file_path)))

        return snapshot_id

    def create_and_upload_manifest(self, tablet_leaders, snapshot_bucket, pg_based_backup):
        """
        Generates and uploads metadata file describing the backup properties.
        :param tablet_leaders: a list of (tablet_id, tserver_ip, tserver_region) tuples
        :param snapshot_bucket: the bucket directory under which to upload the data directories
        """
        self.manifest.init(snapshot_bucket, pg_based_backup)
        if not pg_based_backup:
            self.manifest.init_locations(tablet_leaders, snapshot_bucket)

        # Create Manifest file and upload it to tmp dir on the main host.
        metadata_path = os.path.join(self.get_tmp_dir(), MANIFEST_FILE_NAME)
        self.manifest.save_into_file(metadata_path)
        os.chmod(metadata_path, 0o600)
        if not self.args.local_yb_admin_binary:
            self.upload_local_file_to_server(
                self.get_main_host_ip(), metadata_path, self.get_tmp_dir())

        # Upload Manifest and checksum file from the main host to the target backup path.
        snapshot_filepath = self.snapshot_location(snapshot_bucket)
        target_filepath = os.path.join(snapshot_filepath, MANIFEST_FILE_NAME)
        self.upload_metadata_and_checksum(metadata_path, target_filepath)

    def bg_disable_splitting(self):
        while (True):
            logging.info("Disabling splitting for {} milliseconds.".format(DISABLE_SPLITTING_MS))
            self.disable_tablet_splitting()
            time.sleep(DISABLE_SPLITTING_FREQ_SEC)

    def disable_tablet_splitting(self):
        self.run_yb_admin(["disable_tablet_splitting", str(DISABLE_SPLITTING_MS), "yb_backup"])

    def backup_table(self):
        """
        Creates a backup of the given table by creating a snapshot and uploading it to the provided
        backup location.
        """

        if not self.args.keyspace:
            raise BackupException('Need to specify --keyspace')

        if self.args.table:
            if self.is_ysql_keyspace():
                raise BackupException(
                    "Back up for YSQL is only supported at the database level, "
                    "and not at the table level.")

            logging.info('[app] Backing up tables: {} to {}'.format(
                         self.table_names_str(), self.args.backup_location))
        else:
            if len(self.args.keyspace) != 1:
                raise BackupException(
                    "Only one keyspace supported. Found {} --keyspace keys.".
                    format(len(self.args.keyspace)))
            if self.is_ysql_keyspace() and self.args.skip_indexes:
                raise BackupException(
                    "skip_indexes is only supported for YCQL keyspaces.")
            logging.info('[app] Backing up keyspace: {} to {}'.format(
                         self.args.keyspace[0], self.args.backup_location))

        if self.per_region_backup():
            logging.info('[app] Geo-partitioned backup for regions: {}'.format(self.args.region))

        if self.args.no_auto_name:
            snapshot_bucket = None
        else:
            if self.args.table:
                snapshot_bucket = 'table-{}'.format(self.table_names_str('.', '-'))
            else:
                snapshot_bucket = 'keyspace-{}'.format(self.args.keyspace[0])

            if self.args.table_uuid:
                if len(self.args.table) != len(self.args.table_uuid):
                    raise BackupException(
                        "Found {} --table_uuid keys and {} --table keys. Number of these keys "
                        "must be equal.".format(len(self.args.table_uuid), len(self.args.table)))

                snapshot_bucket = '{}-{}'.format(snapshot_bucket, '-'.join(self.args.table_uuid))

        if not self.args.do_not_disable_splitting:
            disable_splitting_supported = False
            try:
                self.disable_tablet_splitting()
                disable_splitting_supported = True
            except YbAdminOpNotSupportedException as ex:
                # Continue if the disable splitting APIs are not supported, otherwise re-raise and
                # crash.
                logging.warning("disable_tablet_splitting operation was not found in yb-admin.")

            if disable_splitting_supported:
                disable_splitting_thread = threading.Thread(target=self.bg_disable_splitting)
                disable_splitting_thread.start()
                for i in range(IS_SPLITTING_DISABLED_MAX_RETRIES):
                    # Wait for existing splits to complete.
                    output = self.run_yb_admin(["is_tablet_splitting_complete"])
                    if ("is_tablet_splitting_complete: true" in output):
                        break
                    logging.info("Waiting for existing tablet splits to complete.")
                    time.sleep(5)
                else:
                    raise BackupException('Splitting did not complete in time.')

        self.timer.log_new_phase("Create and upload snapshot metadata")
        snapshot_id = self.create_and_upload_metadata_files(snapshot_bucket)

        pg_based_backup = snapshot_id is None
        snapshot_locations = {}
        if self.args.upload:
            if self.args.backup_keys_source:
                self.upload_encryption_key_file()

            snapshot_filepath = self.snapshot_location(snapshot_bucket)
            snapshot_locations["snapshot_url"] = snapshot_filepath

            if pg_based_backup:
                self.create_and_upload_manifest(None, snapshot_bucket, pg_based_backup)
                logging.info("[app] PG based backup successful!")
            else:
                self.timer.log_new_phase("Find tablet leaders")
                tablet_leaders = self.find_tablet_leaders()

                self.timer.log_new_phase("Upload snapshot directories")
                self.upload_snapshot_directories(tablet_leaders, snapshot_id, snapshot_bucket)
                self.create_and_upload_manifest(tablet_leaders, snapshot_bucket, pg_based_backup)
                logging.info("[app] Backed up tables {} to {} successfully!".format(
                    self.table_names_str(), snapshot_filepath))

                if self.per_region_backup():
                    for region in self.args.region:
                        regional_filepath = self.snapshot_location(snapshot_bucket, region)
                        logging.info("[app] Path for region '{}': {}".format(
                            region, regional_filepath))
                        snapshot_locations[region] = regional_filepath

            snapshot_locations["backup_size_in_bytes"] = self.manifest.get_backup_size()
        else:
            snapshot_locations["snapshot_url"] = "UPLOAD_SKIPPED"

        print(json.dumps(snapshot_locations))

    def download_file(self, src_path, target_path):
        """
        Download the file from the external source to the local temporary folder.
        """
        if self.args.local_yb_admin_binary:
            if not self.args.disable_checksums:
                checksum_downloaded = self.checksum_path_downloaded(target_path)
                self.run_program(
                    self.storage.download_file_cmd(self.checksum_path(src_path),
                                                   checksum_downloaded))
            self.run_program(
                self.storage.download_file_cmd(src_path, target_path))

            if not self.args.disable_checksums:
                self.run_program(
                    self.create_checksum_cmd(target_path, self.checksum_path(target_path)))
                check_checksum_res = self.run_program(
                    compare_checksums_cmd(checksum_downloaded,
                                          self.checksum_path(target_path))).strip()
        else:
            server_ip = self.get_main_host_ip()

            if not self.args.disable_checksums:
                checksum_downloaded = self.checksum_path_downloaded(target_path)
                self.run_ssh_cmd(
                    self.storage.download_file_cmd(self.checksum_path(src_path),
                                                   checksum_downloaded),
                    server_ip)
            self.run_ssh_cmd(
                self.storage.download_file_cmd(src_path, target_path),
                server_ip)

            if not self.args.disable_checksums:
                self.run_ssh_cmd(
                    self.create_checksum_cmd(target_path, self.checksum_path(target_path)),
                    server_ip)
                check_checksum_res = self.run_ssh_cmd(
                    compare_checksums_cmd(checksum_downloaded, self.checksum_path(target_path)),
                    server_ip).strip()

        # Todo: Fix the condition. Will be fixed, as part of migration of common
        # ssh_librabry(yugabyte-db/managed/devops/opscli/ybops/utils/ssh.py).
        if (not self.args.disable_checksums) and \
            (check_checksum_res != 'correct' and
             (self.args.ssh2_enabled and 'correct' not in check_checksum_res)):
            raise BackupException('Check-sum for {} is {}'.format(
                target_path, check_checksum_res))

        logging.info(
            'Downloaded metadata file %s from %s' % (target_path, src_path))

    def load_or_create_manifest(self):
        """
        Download the Manifest file for the backup to the local object.
        Create the Manifest by default if it's not available (old backup).
        """
        if self.args.local_yb_admin_binary:
            self.run_program(['mkdir', '-p', self.get_tmp_dir()])
        else:
            self.create_remote_tmp_dir(self.get_main_host_ip())

        src_manifest_path = os.path.join(self.args.backup_location, MANIFEST_FILE_NAME)
        manifest_path = os.path.join(self.get_tmp_dir(), MANIFEST_FILE_NAME)
        try:
            try:
                self.download_file(src_manifest_path, manifest_path)
            except subprocess.CalledProcessError as ex:
                if self.xxhash_checksum_path:
                    # Possibly old checksum tool was used for the backup.
                    # Try again with the old tool.
                    logging.warning("Try to use " + SHA_TOOL_PATH + " for Manifest")
                    self.xxhash_checksum_path = ''
                    self.download_file(src_manifest_path, manifest_path)
                else:
                    raise ex
            self.download_file_from_server(
                self.get_main_host_ip(), manifest_path, manifest_path)
            self.manifest.load_from_file(manifest_path)
        except subprocess.CalledProcessError as ex:
            # The file is available for new backup only.
            if self.args.verbose:
                logging.info("Exception while downloading Manifest file {}. This must be "
                             "a backup from an older version, ignoring: {}".
                             format(src_manifest_path, ex))

        if self.manifest.is_loaded():
            if not self.args.disable_checksums and \
                self.manifest.get_hash_algorithm() == XXH64_FILE_EXT \
                    and not self.xxhash_checksum_path:
                raise BackupException("Manifest references unavailable tool: xxhash")
        else:
            self.manifest.create_by_default(self.args.backup_location)
            self.xxhash_checksum_path = ''

        if self.args.verbose:
            logging.info("{} manifest: {}".format(
                "Loaded" if self.manifest.is_loaded() else "Generated",
                self.manifest.to_string()))

    def download_metadata_file(self):
        """
        Download the metadata file for a backup so as to perform a restore based on it.
        """
        self.load_or_create_manifest()
        dump_files = []

        if ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR:
            restore_tablespaces = self.args.restore_tablespaces
        else:
            restore_tablespaces = self.args.use_tablespaces or self.args.restore_tablespaces

        if restore_tablespaces:
            src_sql_tbsp_dump_path = os.path.join(
                self.args.backup_location, SQL_TBSP_DUMP_FILE_NAME)
            sql_tbsp_dump_path = os.path.join(self.get_tmp_dir(), SQL_TBSP_DUMP_FILE_NAME)
            self.download_file(src_sql_tbsp_dump_path, sql_tbsp_dump_path)
            dump_files.append(sql_tbsp_dump_path)
        else:
            sql_tbsp_dump_path = None

        if self.args.restore_roles:
            src_sql_roles_dump_path = os.path.join(
                self.args.backup_location, SQL_ROLES_DUMP_FILE_NAME)
            sql_roles_dump_path = os.path.join(self.get_tmp_dir(), SQL_ROLES_DUMP_FILE_NAME)
            self.download_file(src_sql_roles_dump_path, sql_roles_dump_path)
            dump_files.append(sql_roles_dump_path)
        else:
            sql_roles_dump_path = None

        src_sql_dump_path = os.path.join(self.args.backup_location, SQL_DUMP_FILE_NAME)
        sql_dump_path = os.path.join(self.get_tmp_dir(), SQL_DUMP_FILE_NAME)
        try:
            self.download_file(src_sql_dump_path, sql_dump_path)
        except subprocess.CalledProcessError as ex:
            if self.is_ysql_keyspace():
                raise ex
            else:
                # Possibly this is YCQL backup (no way to determite it exactly at this point).
                # Try to ignore YSQL dump - YSQL table restoring will fail a bit later
                # on 'import_snapshot' step.
                logging.info("Ignoring the exception in downloading of {}: {}".
                             format(src_sql_dump_path, ex))
                sql_dump_path = None

        if sql_dump_path:
            dump_files.append(sql_dump_path)
            if self.manifest.is_pg_based_backup():
                src_sql_data_dump_path = os.path.join(
                    self.args.backup_location, SQL_DATA_DUMP_FILE_NAME)
                sql_data_dump_path = os.path.join(self.get_tmp_dir(), SQL_DATA_DUMP_FILE_NAME)
                try:
                    self.download_file(src_sql_data_dump_path, sql_data_dump_path)
                except subprocess.CalledProcessError as ex:
                    raise ex
                dump_files.append(sql_data_dump_path)
                logging.info('Skipping ' + METADATA_FILE_NAME + ' metadata file downloading.')
                return (None, dump_files)

        src_metadata_path = os.path.join(self.args.backup_location, METADATA_FILE_NAME)
        metadata_path = os.path.join(self.get_tmp_dir(), METADATA_FILE_NAME)
        self.download_file(src_metadata_path, metadata_path)

        return (metadata_path, dump_files)

    def import_ysql_dump(self, dump_file_path):
        """
        Import the YSQL dump using the provided file.
        """
        on_error_stop = (not self.args.ysql_ignore_restore_errors and
                         ENABLE_STOP_ON_YSQL_DUMP_RESTORE_ERROR)

        old_db_name = None
        if self.args.keyspace or on_error_stop:
            # Get YSQL DB name from the dump file.
            cmd = get_db_name_cmd(dump_file_path)

            if self.args.local_yb_admin_binary:
                old_db_name = self.run_program(cmd).strip()
            else:
                old_db_name = self.run_ssh_cmd(cmd, self.get_main_host_ip()).strip()

            if self.args.ssh2_enabled:
                # Todo: Fix the condition. Will be fixed, as part of migration of common
                # ssh_librabry(yugabyte-db/managed/devops/opscli/ybops/utils/ssh.py).
                old_db_name = old_db_name.splitlines()[-1].strip()

        new_db_name = None
        if self.args.keyspace:
            if old_db_name:
                new_db_name = keyspace_name(self.args.keyspace[0])
                if new_db_name == old_db_name:
                    logging.info("[app] Skip renaming because YSQL DB name was not changed: "
                                 "'{}'".format(old_db_name))
                else:
                    logging.info("[app] Renaming YSQL DB from '{}' into '{}'".format(
                                 old_db_name, new_db_name))
                    cmd = replace_db_name_cmd(dump_file_path, old_db_name,
                                              '"{}"'.format(new_db_name))

                    if self.args.local_yb_admin_binary:
                        self.run_program(cmd)
                    else:
                        self.run_ssh_cmd(cmd, self.get_main_host_ip())
            else:
                logging.info("[app] Skip renaming because YSQL DB name was not found in file "
                             "{}".format(dump_file_path))

        if self.args.edit_ysql_dump_sed_reg_exp:
            logging.info("[app] Applying sed regular expression '{}' to {}".format(
                         self.args.edit_ysql_dump_sed_reg_exp, dump_file_path))
            cmd = apply_sed_edit_reg_exp_cmd(dump_file_path, self.args.edit_ysql_dump_sed_reg_exp)

            if self.args.local_yb_admin_binary:
                self.run_program(cmd)
            else:
                self.run_ssh_cmd(cmd, self.get_main_host_ip())

        ysql_shell_args = ['--echo-all', '--file=' + dump_file_path]

        if self.args.ignore_existing_tablespaces:
            ysql_shell_args.append('--variable=ignore_existing_tablespaces=1')

        if self.args.use_tablespaces:
            ysql_shell_args.append('--variable=use_tablespaces=1')

        if self.args.ignore_existing_roles:
            ysql_shell_args.append('--variable=ignore_existing_roles=1')

        if self.args.use_roles:
            ysql_shell_args.append('--variable=use_roles=1')

        if on_error_stop:
            logging.info(
                "[app] Enable ON_ERROR_STOP mode when applying '{}'".format(dump_file_path))
            ysql_shell_args.append('--variable=ON_ERROR_STOP=1')
            error_msg = None
            try:
                self.run_ysql_shell(ysql_shell_args)
            except subprocess.CalledProcessError as ex:
                output = str(ex.output.decode('utf-8', errors='replace')
                                      .encode("ascii", "ignore")
                                      .decode("ascii"))
                errors = []
                for line in output.splitlines():
                    if YSQL_SHELL_ERROR_RE.match(line):
                        errors.append(line)

                error_msg = "Failed to apply YSQL dump '{}' with RC={}: {}".format(
                    dump_file_path, ex.returncode, '; '.join(errors))
            except Exception as ex:
                error_msg = "Failed to apply YSQL dump '{}': {}".format(dump_file_path, ex)

            if error_msg:
                logging.error("[app] {}".format(error_msg))
                db_name_to_drop = new_db_name if new_db_name else old_db_name
                if db_name_to_drop:
                    logging.info("[app] YSQL DB '{}' will be deleted at exit...".format(
                                 db_name_to_drop))
                    atexit.register(self.delete_created_ysql_db, db_name_to_drop)
                else:
                    logging.warning("[app] Skip new YSQL DB drop because YSQL DB name "
                                    "was not found in file '{}'".format(dump_file_path))

                raise YsqlDumpApplyFailedException(error_msg)
        else:
            logging.warning("[app] Ignoring YSQL errors when applying '{}'".format(dump_file_path))
            self.run_ysql_shell(ysql_shell_args)

        return new_db_name

    def import_snapshot(self, metadata_file_path):
        """
        Import the snapshot metadata using the provided metadata file, process the metadata for
        the imported snapshot and return the snapshot metadata. The snapshot metadata returned is a
        map containing all the metadata for the snapshot and mappings from old ids to new ids for
        table, keyspace, tablets and snapshot.
        """
        yb_admin_args = ['import_snapshot', metadata_file_path]

        if self.args.keyspace:
            yb_admin_args += [self.args.keyspace[0]]

        if self.args.table:
            yb_admin_args += [' '.join(self.args.table)]

        output = self.run_yb_admin(yb_admin_args, run_ip=self.get_main_host_ip())

        snapshot_metadata = {}
        snapshot_metadata['keyspace_name'] = []
        snapshot_metadata['table_name'] = []
        snapshot_metadata['table'] = {}
        snapshot_metadata['tablet'] = {}
        snapshot_metadata['snapshot_id'] = {}
        for idx, line in enumerate(output.splitlines()):
            table_match = IMPORTED_TABLE_RE.search(line)
            if table_match:
                snapshot_metadata['keyspace_name'].append(table_match.group(1))
                snapshot_metadata['table_name'].append(table_match.group(2))
                logging.info('Imported table: {}.{}'.format(table_match.group(1),
                                                            table_match.group(2)))
            elif NEW_OLD_UUID_RE.search(line):
                (entity, old_id, new_id) = split_by_tab(line)
                if entity == 'Table':
                    snapshot_metadata['table'][new_id] = old_id
                    logging.info('Imported table id was changed from {} to {}'.format(old_id,
                                                                                      new_id))
                elif entity.startswith('Tablet'):
                    snapshot_metadata['tablet'][new_id] = old_id
                elif entity == 'Snapshot':
                    snapshot_metadata['snapshot_id']['old'] = old_id
                    snapshot_metadata['snapshot_id']['new'] = new_id
                elif entity == 'ColocatedTable':
                    logging.info('Imported colocated table id was changed from {} to {}'
                                 .format(old_id, new_id))
            elif (COLOCATED_DB_PARENT_TABLE_NEW_OLD_UUID_RE.search(line) or
                  TABLEGROUP_PARENT_TABLE_NEW_OLD_UUID_RE.search(line) or
                  COLOCATION_PARENT_TABLE_NEW_OLD_UUID_RE.search(line) or
                  COLOCATION_MIGRATION_PARENT_TABLE_NEW_OLD_UUID_RE.search(line)):
                # Parent colocated/tablegroup table
                (entity, old_id, new_id) = split_by_tab(line)
                assert entity == 'ParentColocatedTable'
                if (old_id.endswith(TABLEGROUP_UUID_SUFFIX) or
                        old_id.endswith(COLOCATION_UUID_SUFFIX)):
                    verify_tablegroup_parent_table_ids(old_id, new_id)
                snapshot_metadata['table'][new_id] = old_id
                # Colocated parent table includes both tablegroup parent table
                # and colocated database parent table.
                logging.info('Imported colocated parent table id was changed from {} to {}'
                             .format(old_id, new_id))

        tablet_locations = {}
        if self.manifest.is_loaded():
            self.manifest.get_tablet_locations(tablet_locations)
        else:
            default_location = self.args.backup_location
            if self.args.verbose:
                logging.info("Default location for all tablets: {}".format(default_location))

            for tablet_id in snapshot_metadata['tablet'].values():
                tablet_locations[tablet_id] = default_location

        snapshot_metadata['tablet_location'] = tablet_locations
        return snapshot_metadata

    def find_tablet_replicas(self, snapshot_metadata):
        """
        Finds the tablet replicas for tablets present in snapshot_metadata and returns a list of all
        tservers that need to be processed.
        """

        # Parallize this using half of the parallelism setting to not overload master with yb-admin.
        parallelism = min(16, (self.args.parallelism + 1) // 2)
        pool = ThreadPool(parallelism)
        self.pools.append(pool)
        parallel_find_tservers = MultiArgParallelCmd(self.run_yb_admin, verbose=self.args.verbose)

        # First construct all the yb-admin commands to send.
        for new_tablet_id in snapshot_metadata['tablet']:
            parallel_find_tservers.add_args(('list_tablet_servers', new_tablet_id))

        num_loops = 0
        # Continue searching for TServers until all tablet peers are either LEADER or FOLLOWER
        # or READ_REPLICA. This is done to avoid errors later in the restore_snapshot phase.
        while num_loops < REPLICAS_SEARCHING_LOOP_MAX_RETRIES:
            logging.info('[app] Start searching for tablet replicas (try {})'.format(num_loops))
            num_loops += 1
            found_bad_ts = False
            tablets_by_tserver_ip = {}

            # Run all the list_tablet_servers in parallel.
            output = parallel_find_tservers.run(pool)

            # Process the output.
            for cmd in output:
                # Pull the new_id value out from the command string.
                matches = LIST_TABLET_SERVERS_RE.match(str(cmd))
                tablet_id = matches.group(1)
                num_ts = 0

                # For each output line, get the tablet servers ips for this tablet id.
                for line in output[cmd].splitlines():
                    if LEADING_UUID_RE.match(line):
                        fields = split_by_tab(line)
                        (rpc_ip_port, role) = (fields[1], fields[2])
                        (rpc_ip, rpc_port) = rpc_ip_port.rsplit(':', 1)
                        if role == 'LEADER' or role == 'FOLLOWER' or role == 'READ_REPLICA':
                            broadcast_ip = self.rpc_to_broadcast_map[rpc_ip]
                            tablets_by_tserver_ip.setdefault(broadcast_ip, set()).add(tablet_id)
                            num_ts += 1
                        else:
                            # Bad/temporary roles: LEARNER, NON_PARTICIPANT, UNKNOWN_ROLE.
                            broadcast_ip = rpc_ip
                            found_bad_ts = True
                            logging.warning("Found TS {} with bad role: {} for tablet {}. "
                                            "Retry searching.".format(broadcast_ip, role,
                                                                      tablet_id))
                            break

                if found_bad_ts:
                    break
                if num_ts == 0:
                    raise BackupException(
                        "No alive TS found for tablet {}:\n{}".format(tablet_id, output))

            if not found_bad_ts:
                return tablets_by_tserver_ip

            logging.info("Sleep for {} seconds before the next tablet replicas searching round.".
                         format(SLEEP_IN_REPLICAS_SEARCHING_ROUND_SEC))
            time.sleep(SLEEP_IN_REPLICAS_SEARCHING_ROUND_SEC)

        raise BackupException(
            "Exceeded max number of retries for the tablet replicas searching loop ({})!".
            format(REPLICAS_SEARCHING_LOOP_MAX_RETRIES))

    def identify_new_tablet_replicas(self, tablets_by_tserver_ip_old, tablets_by_tserver_ip_new):
        """
        Compare old and new sets of tablets per every TServer, find and return difference.
        Returns union of the sets per TServer, and delta of the sets.
        """

        tablets_by_tserver_union = copy.deepcopy(tablets_by_tserver_ip_old)
        tablets_by_tserver_delta = {}

        for ip in tablets_by_tserver_ip_new:
            tablets = tablets_by_tserver_ip_new[ip]
            if ip in tablets_by_tserver_ip_old:
                if not (tablets_by_tserver_ip_old[ip] >= tablets):
                    tablets_by_tserver_union[ip].update(tablets)
                    tablets_by_tserver_delta[ip] = tablets - tablets_by_tserver_ip_old[ip]
            else:
                tablets_by_tserver_union[ip] = tablets
                tablets_by_tserver_delta[ip] = tablets

        return (tablets_by_tserver_union, tablets_by_tserver_delta)

    def download_snapshot_directories(self, snapshot_meta, tablets_by_tserver_to_download,
                                      snapshot_id, table_ids):
        with terminating(ThreadPool(self.args.parallelism)) as pool:
            self.pools.append(pool)
            self.timer.log_new_phase("Find all table/tablet data dirs on all tservers")
            tserver_ips = list(tablets_by_tserver_to_download.keys())
            data_dir_by_tserver = SingleArgParallelCmd(self.find_data_dirs,
                                                       tserver_ips,
                                                       verbose=self.args.verbose).run(pool)

            if self.args.verbose:
                logging.info('Found data directories: {}'.format(data_dir_by_tserver))

            (tserver_to_tablet_to_snapshot_dirs, tserver_to_deleted_tablets) =\
                self.generate_snapshot_dirs(
                    data_dir_by_tserver, snapshot_id, tablets_by_tserver_to_download, table_ids)

            # Remove deleted tablets from the list of planned to be downloaded tablets.
            for tserver_ip in tserver_to_deleted_tablets:
                deleted_tablets = tserver_to_deleted_tablets[tserver_ip]
                tablets_by_tserver_to_download[tserver_ip] -= deleted_tablets

            self.timer.log_new_phase("Download data")
            parallel_downloads = SequencedParallelCmd(
                self.run_ssh_cmd, preprocess_args_fn=self.join_ssh_cmds,
                handle_errors=True, verbose=self.args.verbose)
            self.prepare_cloud_ssh_cmds(
                parallel_downloads, tserver_to_tablet_to_snapshot_dirs,
                None, snapshot_id, tablets_by_tserver_to_download,
                upload=False, snapshot_metadata=snapshot_meta)

            # Run a sequence of steps for each tablet, handling different tablets in parallel.
            results = parallel_downloads.run(pool)

            for k in results:
                v = results[k]
                if isinstance(v, tuple) and v[0] == 'failed-cmd':
                    assert len(v) == 2
                    (tablet_id, tserver_ip) = v[1]
                    # In case we fail a cmd, don't mark this tablet-tserver pair as succeeded,
                    # instead we will retry in the next round of downloads.
                    tserver_to_deleted_tablets.setdefault(tserver_ip, set()).add(tablet_id)

            return tserver_to_deleted_tablets

    def restore_table(self):
        """
        Restore a table from the backup stored in the given backup path.
        """
        if self.args.keyspace:
            if len(self.args.keyspace) > 1:
                raise BackupException('Only one --keyspace expected for the restore mode.')
        elif self.args.table:
            raise BackupException('Need to specify --keyspace')

        if self.args.region_location is not None:
            raise BackupException('--region_location is not supported for the restore mode.')

        # TODO (jhe): Perform verification for restore_time. Need to check for:
        #  - Verify that the timestamp given fits in the history retention window for the snapshot
        #  - Verify that we are restoring a keyspace/namespace (no individual tables for pitr)

        logging.info('[app] Restoring backup from {}'.format(self.args.backup_location))
        (metadata_file_path, dump_file_paths) = self.download_metadata_file()
        if len(dump_file_paths):
            self.timer.log_new_phase("Create objects via YSQL dumps")

        for dump_file_path in dump_file_paths:
            dump_file = os.path.basename(dump_file_path)
            if dump_file == SQL_ROLES_DUMP_FILE_NAME:
                logging.info('[app] Create roles from {}'.format(dump_file_path))
                self.import_ysql_dump(dump_file_path)
            elif dump_file == SQL_TBSP_DUMP_FILE_NAME:
                logging.info('[app] Create tablespaces from {}'.format(dump_file_path))
                self.import_ysql_dump(dump_file_path)
            elif dump_file == SQL_DUMP_FILE_NAME:
                logging.info('[app] Create YSQL tables from {}'.format(dump_file_path))
                new_db_name = self.import_ysql_dump(dump_file_path)
            elif dump_file == SQL_DATA_DUMP_FILE_NAME:
                # Note: YSQLDump_data must be last in the dump file list.
                self.timer.log_new_phase("Apply complete YSQL data dump")
                ysqlsh_args = ['--file=' + dump_file_path]
                if new_db_name:
                    ysqlsh_args += ['--dbname=' + new_db_name]

                self.run_ysql_shell(ysqlsh_args)

                # Skipping Snapshot loading & restoring because
                # PG based backup means only complete YSQL Data Dump applying.
                logging.info('[app] Restored PG based backup successfully!')
                print(json.dumps({"success": True}))
                return

        self.timer.log_new_phase("Import snapshot")
        snapshot_metadata = self.import_snapshot(metadata_file_path)
        snapshot_id = snapshot_metadata['snapshot_id']['new']
        table_ids = list(snapshot_metadata['table'].keys())

        self.wait_for_snapshot(snapshot_id, 'importing', CREATE_SNAPSHOT_TIMEOUT_SEC, False)

        if not self.args.no_snapshot_deleting:
            logging.info("Snapshot %s will be deleted at exit...", snapshot_id)
            atexit.register(self.delete_created_snapshot, snapshot_id)

        self.timer.log_new_phase("Generate list of tservers for every tablet")
        all_tablets_by_tserver = self.find_tablet_replicas(snapshot_metadata)
        tablets_by_tserver_to_download = all_tablets_by_tserver

        # The loop must stop after a few rounds because the downloading list includes only new
        # tablets for downloading. The downloading list should become smaller with every round
        # and must become empty in the end.
        num_loops = 0
        while tablets_by_tserver_to_download and num_loops < RESTORE_DOWNLOAD_LOOP_MAX_RETRIES:
            num_loops += 1
            logging.info('[app] Downloading tablets onto %d tservers...',
                         len(tablets_by_tserver_to_download))

            if self.args.verbose:
                logging.info('Downloading list: {}'.format(tablets_by_tserver_to_download))

            # Download tablets and get list of deleted tablets.
            tserver_to_deleted_tablets = self.download_snapshot_directories(
                snapshot_metadata, tablets_by_tserver_to_download, snapshot_id, table_ids)

            # Remove deleted tablets from the list of all tablets.
            for tserver_ip in tserver_to_deleted_tablets:
                deleted_tablets = tserver_to_deleted_tablets[tserver_ip]
                all_tablets_by_tserver[tserver_ip] -= deleted_tablets

            self.timer.log_new_phase("Regenerate list of tservers for every tablet")
            tablets_by_tserver_new = self.find_tablet_replicas(snapshot_metadata)
            # Calculate the new downloading list as a subtraction of sets:
            #     downloading_list = NEW_all_tablet_replicas - OLD_all_tablet_replicas
            # And extend the list of all tablets (as unioun of sets) for using it on the next
            # loop iteration:
            #     OLD_all_tablet_replicas = OLD_all_tablet_replicas + NEW_all_tablet_replicas
            #                             = OLD_all_tablet_replicas + downloading_list
            (all_tablets_by_tserver, tablets_by_tserver_to_download) =\
                self.identify_new_tablet_replicas(all_tablets_by_tserver, tablets_by_tserver_new)

        if num_loops >= RESTORE_DOWNLOAD_LOOP_MAX_RETRIES:
            raise BackupException(
                "Exceeded max number of retries for the restore download loop ({})!".
                format(RESTORE_DOWNLOAD_LOOP_MAX_RETRIES))

        # Finally, restore the snapshot.
        logging.info('Downloading is finished. Restoring snapshot %s ...', snapshot_id)
        self.timer.log_new_phase("Restore the snapshot")

        restore_snapshot_args = ['restore_snapshot', snapshot_id]
        # Pass in the timestamp if provided.
        if self.args.restore_time:
            restore_snapshot_args.append(self.args.restore_time)

        output = self.run_yb_admin(restore_snapshot_args)

        # Transaction-aware snapshots use special restaration id with final state RESTORED,
        # while previous implementation uses snapshot id and it's state COMPLETE.
        restoration_id = snapshot_id
        complete_restoration_state = 'COMPLETE'
        for line in output.splitlines():
            restoration_match = RESTORATION_RE.match(line)
            if restoration_match:
                restoration_id = restoration_match.group(1)
                complete_restoration_state = 'RESTORED'
                logging.info('[app] Found restoration id: ' + restoration_id)

        self.wait_for_snapshot(restoration_id, 'restoring', RESTORE_SNAPSHOT_TIMEOUT_SEC, False,
                               complete_restoration_state)

        logging.info('[app] Restored backup successfully!')
        print(json.dumps({"success": True}))

    def delete_backup(self):
        """
        Delete the backup specified by the storage location.
        """
        if not self.args.backup_location:
            raise BackupException('Need to specify --backup_location')

        self.load_or_create_manifest()
        error = None
        for loc in self.manifest.get_locations():
            try:
                self.delete_bucket_obj(loc)
            except Exception as ex:
                logging.warning("Failed to delete '{}'. Error: {}".format(loc, ex))
                error = ex

        if error:
            raise error

        logging.info('[app] Deleted backup %s successfully!', self.args.backup_location)
        print(json.dumps({"success": True}))

    def restore_keys(self):
        """
        Restore universe keys from the backup stored in the given backup path.
        """
        if self.args.restore_keys_destination:
            self.download_encryption_key_file()

        logging.info('[app] Restored backup universe keys successfully!')
        print(json.dumps({"success": True}))

    # At exit callbacks
    def cleanup_temporary_directory(self, tmp_dir):
        """
        Callback run on exit to clean up temporary directories.
        """
        if self.args.verbose:
            logging.info("Removing temporary directory '{}'".format(tmp_dir))

        self.run_program(['rm', '-rf', tmp_dir])

    def cleanup_remote_temporary_directory(self, server_ip, tmp_dir):
        """
        Callback run on exit to clean up temporary directories on remote host.
        """
        if self.args.verbose:
            logging.info("Removing remote temporary directory '{}' on {}".format(
                tmp_dir, server_ip))

        self.run_ssh_cmd(['rm', '-rf', tmp_dir], server_ip)

    def delete_created_snapshot(self, snapshot_id):
        """
        Callback run on exit to delete temporary newly created snapshot.
        """
        if self.args.verbose:
            logging.info("Deleting snapshot %s ...", snapshot_id)

        return self.run_yb_admin(['delete_snapshot', snapshot_id])

    def delete_created_ysql_db(self, db_name):
        """
        Callback run on exit to delete incomplete/partly created YSQL DB.
        """
        if db_name:
            if self.args.ysql_disable_db_drop_on_restore_errors:
                logging.warning("[app] Skip YSQL DB '{}' drop because "
                                "the clean-up is disabled".format(db_name))
            else:
                ysql_shell_args = ['-c', 'DROP DATABASE {};'.format(db_name)]
                logging.info("[app] Do clean-up due to previous error: "
                             "'{}'".format(' '.join(ysql_shell_args)))
                output = self.run_ysql_shell(ysql_shell_args)
                logging.info("YSQL DB drop result: {}".format(output.replace('\n', ' ')))

    def TEST_yb_admin_unsupported_commands(self):
        try:
            self.run_yb_admin(["fake_command"])
            raise BackupException("Expected YbAdminOpNotSupportedException on unsupported command.")
        except YbAdminOpNotSupportedException as ex:
            # Required output for the JsonReader to pass the test.
            print(json.dumps({"success": True}))

    def run(self):
        try:
            self.post_process_arguments()
            if self.args.TEST_yb_admin_unsupported_commands:
                self.TEST_yb_admin_unsupported_commands()
                return

            try:
                self.database_version = YBVersion(self.run_yb_admin(['--version']),
                                                  self.args.verbose)
            except Exception as ex:
                logging.error("Cannot identify YB cluster version. Ignoring the exception: {}".
                              format(ex))

            if self.args.command == 'restore':
                self.restore_table()
            elif self.args.command == 'create':
                self.backup_table()
            elif self.args.command == 'restore_keys':
                self.restore_keys()
            elif self.args.command == 'delete':
                self.delete_backup()
            else:
                logging.error('Command was not specified')
                print(json.dumps({"error": "Command was not specified"}))
        except BackupException as ex:
            print(json.dumps({"error": "Backup exception: {}".format(str(ex))}))
        except Exception as ex:
            print(json.dumps({"error": "Exception: {}".format(str(ex))}))
            traceback.print_exc()
            traceback.print_stack()
        finally:
            self.timer.print_summary()


class RedactingFormatter(logging.Formatter):
    """
    Replaces portions of log with the desired replacements. The class
    uses re.sub(pattern, replacement, orig_str) function to modify original log-lines.
    The object of this class is added to the logging handler.
    Constructor params:
        - orig_formatter: The original formatter for the logging.
        - sub_list: A list of format (matching-pattern, replacement). The format
                    function iterates through this list and makes replacements.
    """
    def __init__(self, orig_formatter, sub_list):
        self.orig_formatter = orig_formatter
        self._sub_list = sub_list

    def format(self, record):
        log_line = self.orig_formatter.format(record)
        for (pattern, sub) in self._sub_list:
            log_line = re.sub(pattern, sub, log_line)
        return log_line

    def __getattr__(self, attr):
        return getattr(self.orig_formatter, attr)


if __name__ == "__main__":
    # Setup logging. By default in the config the output stream is: stream=sys.stderr.
    # Set custom output format and logging level=INFO (DEBUG messages will not be printed).
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
    # Redact access_token param contents, appending to list can handle more redacts.
    # The log-line after formatting: s3cmd ... --access_token=REDACTED ... .
    for handler in logging.root.handlers:
        handler.setFormatter(RedactingFormatter(handler.formatter,
                             [(ACCESS_TOKEN_RE, ACCESS_TOKEN_REDACT_RE)]))

    # Registers the signal handlers.
    yb_backup = YBBackup()
    with terminating(ThreadPool(1)) as pool:
        try:
            # Main thread cannot be blocked to handle signals.
            future = pool.apply_async(yb_backup.run)
            while not future.ready():
                # Prevent blocking by waiting with timeout.
                future.wait(timeout=1)
        finally:
            logging.info("Terminating all threadpool ...")
            yb_backup.terminate_pools()
            logging.shutdown()
