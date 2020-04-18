#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import argparse
import atexit
import copy
import logging
import pipes
import random
import shutil
import string
import subprocess
import time
import json
from argparse import RawDescriptionHelpFormatter
from multiprocessing.pool import ThreadPool

import os
import re
from six import iteritems

TABLET_UUID_LEN = 32
UUID_RE_STR = '[0-9a-f-]{32,36}'
UUID_ONLY_RE = re.compile('^' + UUID_RE_STR + '$')
NEW_OLD_UUID_RE = re.compile(UUID_RE_STR + '[ ]*\t' + UUID_RE_STR)
LEADING_UUID_RE = re.compile('^(' + UUID_RE_STR + r')\b')
FS_DATA_DIRS_ARG_PREFIX = '--fs_data_dirs='
IMPORTED_TABLE_RE = re.compile('Table being imported: ([^\.]*)\.(.*)')

SNAPSHOT_KEYSPACE_RE = re.compile("^[ \t]*Keyspace:.* name='(.*)' type")
SNAPSHOT_TABLE_RE = re.compile("^[ \t]*Table:.* name='(.*)' type")
SNAPSHOT_INDEX_RE = re.compile("^[ \t]*Index:.* name='(.*)' type")

ROCKSDB_PATH_PREFIX = '/yb-data/tserver/data/rocksdb'

SNAPSHOT_DIR_GLOB = '*' + ROCKSDB_PATH_PREFIX + '/table-*/tablet-*.snapshots/*'
SNAPSHOT_DIR_DEPTH = 7
SNAPSHOT_DIR_SUFFIX_RE = re.compile(
    '^.*/tablet-({})[.]snapshots/({})$'.format(UUID_RE_STR, UUID_RE_STR))

TABLE_PATH_PREFIX_TEMPLATE = ROCKSDB_PATH_PREFIX + '/table-{}'

TABLET_MASK = 'tablet-????????????????????????????????'
TABLET_DIR_GLOB = '*' + TABLE_PATH_PREFIX_TEMPLATE + '/' + TABLET_MASK
TABLET_DIR_DEPTH = 6

METADATA_FILE_NAME = 'SnapshotInfoPB'
CLOUD_CFG_FILE_NAME = 'cloud_cfg'
CLOUD_CMD_MAX_RETRIES = 10

CREATE_SNAPSHOT_TIMEOUT_SEC = 60 * 60  # hour
RESTORE_SNAPSHOT_TIMEOUT_SEC = 24 * 60 * 60  # day
SHA_TOOL_PATH = '/usr/bin/sha256sum'
# Try to read home dir from environment variable, else assume it's /home/yugabyte.
YB_HOME_DIR = os.environ.get("YB_HOME_DIR", "/home/yugabyte")
TSERVER_CONF_PATH = os.path.join(YB_HOME_DIR, 'tserver/conf/server.conf')
K8S_DATA_DIRS = ["/mnt/disk0", "/mnt/disk1"]
DEFAULT_REMOTE_YB_ADMIN_PATH = os.path.join(YB_HOME_DIR, 'master/bin/yb-admin')


class BackupException(Exception):
    """A YugaByte backup exception."""
    pass


def split_by_tab(line):
    return [item.replace(' ', '') for item in line.split("\t")]


def quote_cmd_line_for_bash(cmd_line):
    if not isinstance(cmd_line, list) and not isinstance(cmd_line, tuple):
        raise BackupException("Expected a list/tuple, got: [[ {} ]]".format(cmd_line))
    return ' '.join([pipes.quote(str(arg)) for arg in cmd_line])


class SingleArgParallelCmd:
    """
    Invokes a single-argument function on the given set of argument values in a parallel way
    using the given thread pool. Arguments are first deduplicated, so they have to be hashable.
    Example:
        SingleArgParallelCmd(fn, [a, b, c]).run(pool)
        -> run in parallel Thread-1: ->  fn(a)
                           Thread-2: -> fn(b)
                           Thread-3: -> fn(c)
    """
    def __init__(self, fn, args):
        self.fn = fn
        self.args = args

    def run(self, pool):
        fn_args = sorted(set(self.args))
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
    def __init__(self, fn):
        self.fn = fn
        self.args = []

    def add_args(self, *args_tuple):
        assert isinstance(args_tuple, tuple)
        self.args.append(args_tuple)

    def run(self, pool):
        def internal_fn(args_tuple):
            # One tuple - one function run.
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
    def __init__(self, fn):
        self.fn = fn
        self.args = []
        """
        The index is used to return a function call result as the whole command result.
        For example:
            SequencedParallelCmd p(fn)
            p.start_command()
            p.add_args(a1, a2)
            p.add_args(b1, b2)
            p.use_last_fn_result_as_command_result()
            p.add_args(c1, c2)
            p.run(pool)
            -> run -> fn(a1, a2); result = fn(b1, b2); fn(c1, c2); return result
        """
        self.result_fn_call_index = None

    def start_command(self):
        # Start new set of argument tuples.
        self.args.append([])

    def use_last_fn_result_as_command_result(self):
        # Let's remember the last fn call index to return its' result as the command result.
        last_fn_call_index = len(self.args[-1]) - 1
        # All commands in the set must have the same index of the result function call.
        assert (self.result_fn_call_index is None or
                self.result_fn_call_index == last_fn_call_index)
        self.result_fn_call_index = last_fn_call_index

    def add_args(self, *args_tuple):
        assert isinstance(args_tuple, tuple)
        assert len(self.args) > 0, 'Call start_command() before'
        self.args[-1].append(args_tuple)

    def run(self, pool):
        def internal_fn(list_of_arg_tuples):
            assert isinstance(list_of_arg_tuples, list)
            # A list of commands: do it one by one.
            results = []
            for args_tuple in list_of_arg_tuples:
                assert isinstance(args_tuple, tuple)
                results.append(self.fn(*args_tuple))

            if self.result_fn_call_index is None:
                return results
            else:
                assert self.result_fn_call_index < len(results)
                return results[self.result_fn_call_index]

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
    return ''.join(random.choice(string.lowercase) for i in range(length))


def strip_dir(dir_path):
    return dir_path.rstrip('/\\')


def checksum_path(file_path):
    return file_path + '.sha256'


def checksum_path_downloaded(file_path):
    return checksum_path(file_path) + '.downloaded'


def create_checksum_cmd_not_quoted(file_path, checksum_file_path):
    return "{} {} > {}".format(pipes.quote(SHA_TOOL_PATH), file_path, checksum_file_path)


def create_checksum_cmd(file_path, checksum_file_path):
    return create_checksum_cmd_not_quoted(pipes.quote(file_path), pipes.quote(checksum_file_path))


def create_checksum_cmd_for_dir(dir_path):
    return create_checksum_cmd_not_quoted(os.path.join(pipes.quote(strip_dir(dir_path)), '[!i]*'),
                                          pipes.quote(checksum_path(strip_dir(dir_path))))


# TODO: get rid of this sed / test program generation in favor of a more maintainable solution.
def key_and_file_filter(checksum_file):
    return "\" $( sed 's| .*/| |' {} ) \"".format(pipes.quote(checksum_file))


# TODO: get rid of this sed / test program generation in favor of a more maintainable solution.
def compare_checksums_cmd(checksum_file1, checksum_file2):
    return "test {} = {} && echo correct || echo invalid".format(
        key_and_file_filter(checksum_file1), key_and_file_filter(checksum_file2))


def get_table_names_str(keyspaces, tables, delimeter, space):
    if len(keyspaces) != len(tables):
        raise BackupException(
            "Found {} --keyspace keys and {} --table keys. Number of these keys "
            "must be equal.".format(len(keyspaces), len(tables)))

    table_names = []
    for i in range(0, len(tables)):
        table_names.append(delimeter.join([keyspaces[i], tables[i]]))

    return space.join(table_names)


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
        return "azcopy cp"

    def upload_file_cmd(self, src, dest):
        # azcopy requires quotes around the src and dest. This format is necessary to do so.
        src = "'{}'".format(src)
        dest = "'{}'".format(dest + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        return ["{} {} {}".format(self._command_list_prefix(), src, dest)]

    def download_file_cmd(self, src, dest):
        src = "'{}'".format(src + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        dest = "'{}'".format(dest)
        return ["{} {} {}".format(self._command_list_prefix(), src, dest)]

    def upload_dir_cmd(self, src, dest):
        # azcopy will download the top-level directory as well as the contents without "/*".
        src = "'{}'".format(os.path.join(src, '*'))
        dest = "'{}'".format(dest + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        return ["{} {} {} {}".format(self._command_list_prefix(), src, dest, "--recursive")]

    def download_dir_cmd(self, src, dest):
        src = "'{}'".format(os.path.join(src, '*') + os.getenv('AZURE_STORAGE_SAS_TOKEN'))
        dest = "'{}'".format(dest)
        return ["{} {} {} {}".format(self._command_list_prefix(), src, dest, "--recursive")]


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
        return self._command_list_prefix() + ["-m", "rsync", "-r", src, dest]

    def download_dir_cmd(self, src, dest):
        return self._command_list_prefix() + ["-m", "rsync", "-r", src, dest]


class S3BackupStorage(AbstractBackupStorage):
    def __init__(self, options):
        super(S3BackupStorage, self).__init__(options)

    @staticmethod
    def storage_type():
        return 's3'

    def _command_list_prefix(self):
        # If 's3cmd get' fails it creates zero-length file, '--force' is needed to
        # override this empty file on the next retry-step.
        return ['s3cmd', '--force', '--config=%s' % self.options.cloud_cfg_file_path]

    def upload_file_cmd(self, src, dest):
        cmd_list = ["put", src, dest]
        if self.options.args.sse:
            cmd_list.append("--server-side-encryption")
        return self._command_list_prefix() + cmd_list

    def download_file_cmd(self, src, dest):
        return self._command_list_prefix() + ["get", src, dest]

    def upload_dir_cmd(self, src, dest):
        cmd_list = ["sync", src, dest]
        if self.options.args.sse:
            cmd_list.append("--server-side-encryption")
        return self._command_list_prefix() + cmd_list

    def download_dir_cmd(self, src, dest):
        return self._command_list_prefix() + ["sync", src, dest]


class NfsBackupStorage(AbstractBackupStorage):
    def __init__(self, options):
        super(NfsBackupStorage, self).__init__(options)

    @staticmethod
    def storage_type():
        return 'nfs'

    def _command_list_prefix(self):
        return ['rsync', '-avhW', '--no-compress']

    # This is a single string because that's what we need for doing `mkdir && rsync`.
    def upload_file_cmd(self, src, dest):
        return ["mkdir -p {} && {} {} {}".format(
            os.path.dirname(dest), " ".join(self._command_list_prefix()),
            pipes.quote(src), pipes.quote(dest))]

    def download_file_cmd(self, src, dest):
        return self._command_list_prefix() + [src, dest]

    # This is a list of single string, because a) we need a single string for executing
    # `mkdir && rsync` and b) we need a list of 1 element, as it goes through a tuple().
    def upload_dir_cmd(self, src, dest):
        return ["mkdir -p {} && {} {} {}".format(
            dest, " ".join(self._command_list_prefix()),
            pipes.quote(src), pipes.quote(dest))]

    def download_dir_cmd(self, src, dest):
        return self._command_list_prefix() + [src, dest]


BACKUP_STORAGE_ABSTRACTIONS = {
    S3BackupStorage.storage_type(): S3BackupStorage,
    NfsBackupStorage.storage_type(): NfsBackupStorage,
    GcsBackupStorage.storage_type(): GcsBackupStorage,
    AzBackupStorage.storage_type(): AzBackupStorage
}


class KubernetesDetails():
    def __init__(self, server_fqdn, config_map):
        self.namespace = server_fqdn.split('.')[2]
        self.pod_name = server_fqdn.split('.')[0]
        # The pod names are yb-master-n/yb-tserver-n where n is the pod number
        # and yb-master/yb-tserver are the container names.
        self.container = self.pod_name.rsplit('-', 1)[0]
        self.env_config = os.environ.copy()
        self.env_config["KUBECONFIG"] = config_map[self.namespace]


class YBBackup:
    def __init__(self):
        self.leader_master_ip = ''
        self.tmp_dir_name = ''
        self.server_ips_with_uploaded_cloud_cfg = {}
        self.k8s_namespace_to_cfg = {}
        self.parse_arguments()

    def sleep_or_raise(self, num_retry, timeout, ex):
        if num_retry > 0:
            logging.info("Sleep {}... ({} retries left)".format(timeout, num_retry))
            time.sleep(timeout)
        else:
            raise ex

    def run_program(self, args, num_retry=1, timeout=10, env=None, **kwargs):
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
                if env is None:
                    env = os.environ.copy()
                subprocess_result = subprocess.check_output(args, stderr=subprocess.STDOUT,
                                                            env=env, **kwargs)

                if self.args.verbose:
                    logging.info(
                        "Output from running command [[ {} ]]:\n{}\n[[ END OF OUTPUT ]]".format(
                            cmd_as_str, subprocess_result))
                return subprocess_result
            except subprocess.CalledProcessError as e:
                logging.error("Failed to run command [[ {} ]]: code={} output={}".format(
                    cmd_as_str, e.returncode, e.output))
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
                   "Keys --keyspace, --table and --table_uuid can be repeated several times,\n"
                   "recommended order: --keyspace ks1 --table tbl1 --table_uuid uuid1 "
                   "--keyspace ks2 --table tbl2 --table_uuid uuid2 ...",
            formatter_class=RawDescriptionHelpFormatter)

        parser.add_argument(
            '--masters', required=True,
            help="Comma separated list of masters for the cluster")
        parser.add_argument(
            '--k8s_config', required=False,
            help="Namespace to use for kubectl in case of kubernetes deployment")
        parser.add_argument(
            '--keyspace', action='append',
            help="Repeatable keyspace of the tables to backup or restore")
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
            '--ssh_key_path', required=False, help="Path to the ssh key file")
        parser.add_argument(
            '--ssh_user', default='centos', help="Username to use for the ssh connection.")
        parser.add_argument(
            '--ssh_port', default='54422', help="Port to use for the ssh connection.")

        backup_location_group = parser.add_mutually_exclusive_group(required=True)
        backup_location_group.add_argument(
            '--backup_location',
            help="Directory/bucket under which the snapshots should be created or "
                 "an exact snapshot directory in case of snapshot restoring.")
        # Deprecated flag for backwards compatibility.
        backup_location_group.add_argument('--s3bucket', required=False, help=argparse.SUPPRESS)
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
            '--storage_type', choices=BACKUP_STORAGE_ABSTRACTIONS.keys(),
            default=S3BackupStorage.storage_type(),
            help="Storage backing for backups, eg: s3, nfs, gcs, ..")
        parser.add_argument(
            'command', choices=['create', 'restore'],
            help='Create or restore the backup from the provided backup location.')
        parser.add_argument(
            '--certs_dir', required=False,
            help="The directory containing the certs for secure connections.")
        parser.add_argument(
            '--sse', required=False, action='store_true',
            help='Enable server side encryption on storage')
        logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
        self.args = parser.parse_args()

    def post_process_arguments(self):
        if self.args.verbose:
            logging.info("Parsed arguments: {}".format(vars(self.args)))

        self.args.backup_location = self.args.backup_location or self.args.s3bucket
        options = BackupOptions(self.args)
        self.cloud_cfg_file_path = os.path.join(self.get_tmp_dir(), CLOUD_CFG_FILE_NAME)
        if self.is_s3():
            if not os.getenv('AWS_SECRET_ACCESS_KEY') and not os.getenv('AWS_ACCESS_KEY_ID'):
                with open(self.cloud_cfg_file_path, 'w') as s3_cfg:
                    s3_cfg.write('[default]\n' +
                                 'access_key = ' + '\n' +
                                 'secret_key = ' + '\n' +
                                 'security_token = ' + '\n')
            elif os.getenv('AWS_SECRET_ACCESS_KEY') and os.getenv('AWS_ACCESS_KEY_ID'):
                with open(self.cloud_cfg_file_path, 'w') as s3_cfg:
                    s3_cfg.write('[default]\n' +
                                 'access_key = ' + os.environ['AWS_ACCESS_KEY_ID'] + '\n' +
                                 'secret_key = ' + os.environ['AWS_SECRET_ACCESS_KEY'] + '\n')
            else:
                raise BackupException(
                    "Missing either AWS access key or secret key for S3 "
                    "in AWS_ACCESS_KEY_ID or AWS_SECRET_ACCESS_KEY environment variables.")

            os.chmod(self.cloud_cfg_file_path, 0o400)
            options.cloud_cfg_file_path = self.cloud_cfg_file_path
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
            if '?sv' not in sas_token:
                raise BackupException(
                    "SAS tokens must begin with '?sv'.")

        self.storage = BACKUP_STORAGE_ABSTRACTIONS[self.args.storage_type](options)

        if self.is_k8s():
            self.k8s_namespace_to_cfg = json.loads(self.args.k8s_config)
            if self.k8s_namespace_to_cfg is None:
                raise BackupException("Couldn't load k8s configs")

    def table_names_str(self, delimeter='.', space=' '):
        return get_table_names_str(self.args.keyspace, self.args.table, delimeter, space)

    def is_s3(self):
        return self.args.storage_type == S3BackupStorage.storage_type()

    def is_gcs(self):
        return self.args.storage_type == GcsBackupStorage.storage_type()

    def is_az(self):
        return self.args.storage_type == AzBackupStorage.storage_type()

    def is_k8s(self):
        return self.args.k8s_config is not None

    def is_cloud(self):
        return self.args.storage_type != NfsBackupStorage.storage_type()

    def has_cfg_file(self):
        return self.args.storage_type in [
            GcsBackupStorage.storage_type(), S3BackupStorage.storage_type()]

    def get_leader_master_ip(self):
        if not self.leader_master_ip:
            all_masters = self.args.masters.split(",")
            # Use first Master's ip in list to get list of all masters.
            # self.leader_master_ip, port) = all_masters[0].split(':')[0]
            self.leader_master_ip = all_masters[0].split(':')[0]

            # Get LEADER ip, if it's ALIVE, else any alive master ip.
            output = self.run_yb_admin(['list_all_masters'])
            for line in output.splitlines():
                if LEADING_UUID_RE.match(line):
                    (uuid, ip_port, state, role) = split_by_tab(line)
                    (ip, port) = ip_port.split(':')
                    if state == 'ALIVE':
                        alive_master_ip = ip
                    if role == 'LEADER':
                        break
            self.leader_master_ip = alive_master_ip

        return self.leader_master_ip

    def run_yb_admin(self, cmd_line_args):
        """
        Runs the yb-admin utility from the configured location.
        :param cmd_line_args: command-line arguments to yb-admin
        :return: the standard output of yb-admin
        """

        # Specify cert file in case TLS is enabled.
        cert_flag = []
        if self.args.certs_dir:
            cert_flag = ["--certs_dir_name", self.args.certs_dir]
            cmd_line_args = cert_flag + cmd_line_args

        # Use local yb-admin tool if it's specified.
        if self.args.local_yb_admin_binary:
            if not os.path.exists(self.args.local_yb_admin_binary):
                raise BackupException(
                    "yb-admin binary not found at {}".format(self.args.local_yb_admin_binary))

            return self.run_program(
                [self.args.local_yb_admin_binary, '--master_addresses', self.args.masters] +
                cmd_line_args, num_retry=10)
        else:
            # Using remote yb-admin binary on leader master server.
            return self.run_ssh_cmd(
                [self.args.remote_yb_admin_binary, '--master_addresses', self.args.masters] +
                cmd_line_args,
                self.get_leader_master_ip(),
                num_ssh_retry=10)

    def create_snapshot(self):
        """
        Creates a new snapshot of the configured table.
        :return: snapshot id
        """
        output = self.run_yb_admin(
            ['create_snapshot'] + self.table_names_str(' ').split(' '))
        # Ignores any string before and after the creation string + uuid.
        # \S\s matches every character including newlines.
        matched = re.match(r'[\S\s]*Started snapshot creation: (?P<uuid>.*)', output)
        if not matched:
            raise BackupException(
                    "Couldn't parse create snapshot output! Expected "
                    "'Started snapshot creation: <id>' in the end: {}".format(output))
        snapshot_id = matched.group('uuid')
        if not UUID_ONLY_RE.match(snapshot_id):
            raise BackupException("Did not get a valid snapshot id out of yb-admin output:\n" +
                                  output)
        return snapshot_id

    def wait_for_snapshot(self, snapshot_id, op, timeout_sec, update_table_list):
        """
        Waits for the given snapshot to finish being created or restored.
        """
        start_time = time.time()
        snapshot_done = False
        snapshot_tables = []
        snapshot_keyspaces = []

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
            #   {"type":"NAMESPACE","id":"e4c5591446db417f83a52c679de03118","data":{"name":"a",...}}
            #   {"type":"TABLE","id":"28b5cebe9b0c4cdaa70ce9ceab31b1e5","data":{\
            #       "name":"t2idx","indexed_table_id":"d9603c2cab0b48ec807936496ac0e70e",...}}
            # c1ad61bf-a42b-4bbb-94f9-28516985c2c5  COMPLETE
            #   ...
            for line in output.splitlines():
                if not snapshot_done:
                    if line.find(snapshot_id) == 0:
                        (found_snapshot_id, state) = line.split()
                        if found_snapshot_id == snapshot_id and state == 'COMPLETE':
                            snapshot_done = True
                            if not update_table_list:
                                break
                elif update_table_list:
                    if line[0] == ' ':
                        loaded_json = json.loads(line)
                        object_type = loaded_json['type']
                        if object_type == 'NAMESPACE':
                            snapshot_keyspaces.append(loaded_json['data']['name'])
                        elif object_type == 'TABLE':
                            snapshot_tables.append(loaded_json['data']['name'])
                    else:
                        break  # Break search on the next snapshot id/state line.

            if not snapshot_done:
                logging.info('Waiting for snapshot %s to complete...' % (op))
                time.sleep(5)

        if not snapshot_done:
            raise BackupException('Timed out waiting for snapshot!')

        if update_table_list:
            if len(snapshot_keyspaces) != len(snapshot_tables) or len(snapshot_tables) == 0:
                raise BackupException(
                    "In the snapshot found {} keyspaces and {} tables. The numbers must be equal "
                    "and more than zero.".format(len(snapshot_keyspaces), len(snapshot_tables)))

            self.args.keyspace = snapshot_keyspaces
            self.args.table = snapshot_tables
            logging.info('Updated list of processing tables: ' + self.table_names_str())

        logging.info('Snapshot id %s %s completed successfully' % (snapshot_id, op))

    def find_tablet_leaders(self):
        """
        Lists all tablets and their leaders for the table of interest.
        :return: a list of (tablet id, leader host) tuples
        """
        tablet_leaders = []

        for i in range(0, len(self.args.table)):
            output = self.run_yb_admin(
                ['list_tablets', self.args.keyspace[i], self.args.table[i], '0'])
            for line in output.splitlines():
                if LEADING_UUID_RE.match(line):
                    (tablet_id, partition, tablet_leader_host_port) = split_by_tab(line)
                    ts_host, ts_port = tablet_leader_host_port.split(":")
                    tablet_leaders.append((tablet_id, ts_host))

        return tablet_leaders

    def create_remote_tmp_dir(self, server_ip):
        if self.args.verbose:
            logging.info("Creating {} on server {}".format(self.get_tmp_dir(), server_ip))

        atexit.register(self.cleanup_remote_temporary_directory, server_ip, self.get_tmp_dir())

        return self.run_ssh_cmd(['mkdir', '-p', self.get_tmp_dir()],
                                server_ip, upload_cloud_cfg=False)

    def upload_cloud_config(self, server_ip):
        if server_ip not in self.server_ips_with_uploaded_cloud_cfg:
            if self.args.verbose:
                logging.info(
                    "Uploading {} to server {}".format(self.cloud_cfg_file_path, server_ip))

            this_script_dir = os.path.dirname(os.path.realpath(__file__))
            ssh_wrapper_path = os.path.join(this_script_dir, 'ssh_wrapper_with_sudo.sh')

            output = self.create_remote_tmp_dir(server_ip)
            if self.is_k8s():
                k8s_details = KubernetesDetails(server_ip, self.k8s_namespace_to_cfg)
                output += self.run_program([
                    'kubectl',
                    'cp',
                    self.cloud_cfg_file_path,
                    '{}/{}:{}'.format(
                        k8s_details.namespace, k8s_details.pod_name, self.get_tmp_dir()),
                    '-c',
                    k8s_details.container
                ], env=k8s_details.env_config)
            else:
                output += self.run_program(
                    ['scp',
                     '-S', ssh_wrapper_path,
                     '-o', 'StrictHostKeyChecking=no',
                     '-o', 'UserKnownHostsFile=/dev/null',
                     '-i', self.args.ssh_key_path,
                     '-P', self.args.ssh_port,
                     '-q',
                     self.cloud_cfg_file_path,
                     '%s@%s:%s' % (self.args.ssh_user, server_ip, self.get_tmp_dir())])

            self.server_ips_with_uploaded_cloud_cfg[server_ip] = output

            if self.args.verbose:
                logging.info("Uploading {} to server {} done: {}".format(
                    self.cloud_cfg_file_path, server_ip, output))

    def run_ssh_cmd(self, cmd, server_ip, upload_cloud_cfg=True, num_ssh_retry=3):
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

        if self.is_k8s():
            k8s_details = KubernetesDetails(server_ip, self.k8s_namespace_to_cfg)
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
                env=k8s_details.env_config)
        else:
            return self.run_program([
                'ssh',
                '-o', 'StrictHostKeyChecking=no',
                '-o', 'UserKnownHostsFile=/dev/null',
                '-i', self.args.ssh_key_path,
                '-p', self.args.ssh_port,
                '-q',
                '%s@%s' % (self.args.ssh_user, server_ip),
                'cd / && sudo -u yugabyte bash -c ' + pipes.quote(cmd)],
                num_retry=num_retries)

    def find_data_dirs(self, tserver_ip):
        """
        Finds the data directories on the given tserver. This just reads a config file on the target
        server.
        :param tserver_ip: tablet server ip
        :return: a list of top-level YB data directories
        """
        # TODO(bogdan): figure out at runtime??
        if self.is_k8s():
            return K8S_DATA_DIRS

        grep_output = self.run_ssh_cmd(
            ['egrep', '^' + FS_DATA_DIRS_ARG_PREFIX, TSERVER_CONF_PATH],
            tserver_ip).strip()
        data_dirs = []
        for line in grep_output.split("\n"):
            if line.startswith(FS_DATA_DIRS_ARG_PREFIX):
                for data_dir in line[len(FS_DATA_DIRS_ARG_PREFIX):].split(','):
                    data_dir = data_dir.strip()
                    if data_dir:
                        data_dirs.append(data_dir)
        if not data_dirs:
            raise BackupException(
                ("Did not find any data directories in tablet server config at '{}' on server "
                 "'{}'. Was looking for '{}', got this: [[ {} ]]").format(
                    TSERVER_CONF_PATH, tserver_ip, FS_DATA_DIRS_ARG_PREFIX, grep_output))
        return data_dirs

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

        for tserver_ip, tablets in tablets_by_tserver_ip.iteritems():
            tablet_dirs = []
            data_dirs = data_dir_by_tserver[tserver_ip]
            deleted_tablets = deleted_tablets_by_tserver_ip.setdefault(tserver_ip, set())

            for table_id in table_ids:
                for data_dir in data_dirs:
                    # Find all tablets for this table on this TS in this data_dir:
                    output = self.run_ssh_cmd(
                        ['find', data_dir,
                         '-mindepth', TABLET_DIR_DEPTH,
                         '-maxdepth', TABLET_DIR_DEPTH,
                         '-name', TABLET_MASK,
                         '-and',
                         '-wholename', TABLET_DIR_GLOB.format(table_id)],
                        tserver_ip)
                    tablet_dirs += [line.strip() for line in output.split("\n") if line.strip()]

            if self.args.verbose:
                logging.info("Found tablet directories for table '{}' on  tablet server '{}': {}"
                             .format(table_id, tserver_ip, tablet_dirs))

            if not tablet_dirs:
                logging.error("No tablet directory found for table '{}' on "
                              "tablet server '{}'.".format(table_id, tserver_ip))
                raise BackupException("Tablets for table " + table_id
                                      + " not found on tablet server " + tserver_ip)

            tablet_id_to_snapshot_dirs =\
                tserver_ip_to_tablet_id_to_snapshot_dirs.setdefault(tserver_ip, {})

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
                    deleted_tablets.add(tablet_id)
                    if self.args.verbose:
                        logging.info("Tablet '{}' directory in table '{}' was not found on "
                                     "tablet server '{}'.".format(tablet_id, table_id, tserver_ip))

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
        output = self.run_ssh_cmd(
            ['find', data_dir,
             '-mindepth', SNAPSHOT_DIR_DEPTH,
             '-maxdepth', SNAPSHOT_DIR_DEPTH,
             '-name', snapshot_id, '-and',
             '-wholename', SNAPSHOT_DIR_GLOB],
            tserver_ip)
        return [line.strip() for line in output.split("\n") if line.strip()]

    def upload_snapshot_directories(self, tablet_leaders, snapshot_id, snapshot_filepath):
        """
        Uploads snapshot directories from all tablet servers hosting our table to subdirectories
        of the given target backup directory.
        :param tablet_leaders: a list of (tablet_id, tserver_ip) pairs
        :param snapshot_id: self-explanatory
        :param snapshot_filepath: the top-level directory under which to upload the data directories
        """
        pool = ThreadPool(self.args.parallelism)

        tablets_by_leader_ip = {}
        for (tablet_id, leader_ip) in tablet_leaders:
            tablets_by_leader_ip.setdefault(leader_ip, set()).add(tablet_id)

        tserver_ips = sorted(tablets_by_leader_ip.keys())
        data_dir_by_tserver = SingleArgParallelCmd(self.find_data_dirs, tserver_ips).run(pool)

        parallel_find_snapshots = MultiArgParallelCmd(self.find_snapshot_directories)
        for tserver_ip in tserver_ips:
            data_dirs = data_dir_by_tserver[tserver_ip]
            for data_dir in data_dirs:
                parallel_find_snapshots.add_args(data_dir, snapshot_id, tserver_ip)

        find_snapshot_dir_results = parallel_find_snapshots.run(pool)

        leader_ip_to_tablet_id_to_snapshot_dirs = self.rearrange_snapshot_dirs(
            find_snapshot_dir_results, snapshot_id, tablets_by_leader_ip)

        parallel_uploads = SequencedParallelCmd(self.run_ssh_cmd)
        self.prepare_cloud_ssh_cmds(
             parallel_uploads, leader_ip_to_tablet_id_to_snapshot_dirs, snapshot_filepath,
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
        for (data_dir, snapshot_id_unused, tserver_ip), snapshot_dirs in \
                iteritems(find_snapshot_dir_results):
            assert snapshot_id_unused == snapshot_id
            tablet_id_to_snapshot_dirs =\
                tserver_ip_to_tablet_id_to_snapshot_dirs.setdefault(tserver_ip, {})

            for snapshot_dir in snapshot_dirs:
                suffix_match = SNAPSHOT_DIR_SUFFIX_RE.match(snapshot_dir)
                if not suffix_match:
                    raise BackupException(
                        ("Could not parse tablet id and snapshot id out of snapshot "
                         "directory: '{}'").format(snapshot_dir))
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
                    logging.warning(
                        ("Found a snapshot directory '{}' on tablet server '{}' that is not "
                         "present in the list of tablets we are interested in that have this "
                         "tserver hosting it ({}), skipping.").format(
                            snapshot_dir, tserver_ip,
                            ", ".join(sorted(tablets_by_tserver_ip[tserver_ip]))))
                    continue

                tablet_id_to_snapshot_dirs.setdefault(tablet_id, set()).add(snapshot_dir)

        return tserver_ip_to_tablet_id_to_snapshot_dirs

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
        logging.info('Creating check-sum for %s on tablet server %s' % (
                     snapshot_dir, tserver_ip))
        create_checksum_cmd = create_checksum_cmd_for_dir(snapshot_dir)

        target_tablet_filepath = os.path.join(snapshot_filepath, 'tablet-%s' % (tablet_id))
        target_checksum_filepath = checksum_path(target_tablet_filepath)
        snapshot_dir_checksum = checksum_path(strip_dir(snapshot_dir))
        logging.info('Uploading %s from tablet server %s to %s' % (
                     snapshot_dir_checksum, tserver_ip, target_checksum_filepath))
        upload_checksum_cmd = self.storage.upload_file_cmd(
            snapshot_dir_checksum, target_checksum_filepath)

        target_filepath = target_tablet_filepath + '/'
        logging.info('Uploading %s from tablet server %s to %s' % (
                     snapshot_dir, tserver_ip, target_filepath))
        upload_tablet_cmd = self.storage.upload_dir_cmd(snapshot_dir, target_filepath)

        logging.info('Uploading from %s to %s URL %s on tablet server %s' % (snapshot_dir,
                     self.args.storage_type, snapshot_filepath, tserver_ip))
        # Commands to be run on TSes over ssh for uploading the tablet backup.
        # 1. Create check-sum file (via sha256sum tool).
        parallel_commands.add_args(create_checksum_cmd, tserver_ip)
        # 2. Upload check-sum file.
        parallel_commands.add_args(tuple(upload_checksum_cmd), tserver_ip)
        # 3. Upload tablet folder.
        parallel_commands.add_args(tuple(upload_tablet_cmd), tserver_ip)

    def prepare_download_command(self, parallel_commands, snapshot_filepath, tablet_id,
                                 tserver_ip, snapshot_dir, snapshot_metadata):
        """
        Prepares the command to download the backup files to the tservers.

        :param parallel_commands: result parallel commands to run.
        :param snapshot_filepath: Filepath/cloud url where the backup is stored.
        :param tablet_id: tablet_id for the tablet whose data we would like to download.
        :param tserver_ip: tserver ip from which the data needs to be downloaded.
        :param snapshot_dir: The snapshot directory on the tserver to which we need to download.
        """
        if tablet_id not in snapshot_metadata['tablet']:
            raise BackupException('Could not find metadata for tablet id {}'.format(tablet_id))

        old_tablet_id = snapshot_metadata['tablet'][tablet_id]
        source_filepath = os.path.join(snapshot_filepath, 'tablet-%s/' % (old_tablet_id))
        snapshot_dir_tmp = strip_dir(snapshot_dir) + '.tmp/'
        logging.info('Downloading %s from %s to %s on tablet server %s' % (source_filepath,
                     self.args.storage_type, snapshot_dir_tmp, tserver_ip))

        # Download the data to a tmp directory and then move it in place.
        cmd = self.storage.download_dir_cmd(source_filepath, snapshot_dir_tmp)

        source_checksum_filepath = checksum_path(
            os.path.join(snapshot_filepath, 'tablet-%s' % (old_tablet_id)))
        snapshot_dir_checksum = checksum_path_downloaded(strip_dir(snapshot_dir))
        cmd_checksum = self.storage.download_file_cmd(
            source_checksum_filepath, snapshot_dir_checksum)

        create_checksum_cmd = create_checksum_cmd_for_dir(snapshot_dir_tmp)
        check_checksum_cmd = compare_checksums_cmd(
            snapshot_dir_checksum, checksum_path(strip_dir(snapshot_dir_tmp)))

        rmcmd = ['rm', '-rf', snapshot_dir]
        mkdircmd = ['mkdir', '-p', snapshot_dir_tmp]
        mvcmd = ['mv', snapshot_dir_tmp, snapshot_dir]

        # Commands to be run over ssh for downloading the tablet backup.
        # 1. Clean-up: delete target tablet folder.
        parallel_commands.add_args(tuple(rmcmd), tserver_ip)
        # 2. Create temporary snapshot dir.
        parallel_commands.add_args(tuple(mkdircmd), tserver_ip)
        # 3. Download tablet folder.
        parallel_commands.add_args(tuple(cmd), tserver_ip)
        # 4. Download check-sum file.
        parallel_commands.add_args(tuple(cmd_checksum), tserver_ip)
        # 5. Create new check-sum file.
        parallel_commands.add_args(create_checksum_cmd, tserver_ip)
        # 6. Compare check-sum files.
        parallel_commands.add_args(check_checksum_cmd, tserver_ip)
        parallel_commands.use_last_fn_result_as_command_result()
        # 7. Move the backup in place.
        parallel_commands.add_args(tuple(mvcmd), tserver_ip)

    def prepare_cloud_ssh_cmds(
            self, parallel_commands, tserver_ip_to_tablet_id_to_snapshot_dirs, snapshot_filepath,
            snapshot_id, tablets_by_tserver_ip, upload, snapshot_metadata):
        """
        Prepares cloud_command-over-ssh command lines for uploading the snapshot.

        :param parallel_commands: result parallel commands to run.
        :param tserver_ip_to_tablet_id_to_snapshot_dirs: the three-level map as returned by
            rearrange_snapshot_dirs.
        :param snapshot_filepath: the top-level cloud URL to create snapshot directories under
        :param snapshot_id: the snapshot id we're dealing with
        :param tablets_by_tserver_ip: a map from tserver ip to all tablet ids that tserver is the
            responsible for.
        :param upload: True if we are uploading files to cloud, false if we are downloading files
            from cloud.
        :param snapshot_metadata: In case of downloading files from cloud to restore a backup,
            this is the snapshot metadata stored in cloud for the backup.
        """
        for tserver_ip, tablet_id_to_snapshot_dirs in \
                iteritems(tserver_ip_to_tablet_id_to_snapshot_dirs):
            tablet_ids_with_data_dirs = set()
            for tablet_id, snapshot_dirs in iteritems(tablet_id_to_snapshot_dirs):
                if len(snapshot_dirs) > 1:
                    raise BackupException(
                        ('Found multiple snapshot directories on tserver {} for snapshot id '
                         '{}: {}').format(tserver_ip, snapshot_id, snapshot_dirs))
                assert len(snapshot_dirs) == 1
                snapshot_dir = list(snapshot_dirs)[0] + '/'
                parallel_commands.start_command()

                if upload:
                    self.prepare_upload_command(
                        parallel_commands, snapshot_filepath, tablet_id, tserver_ip, snapshot_dir)
                else:
                    self.prepare_download_command(
                        parallel_commands, snapshot_filepath, tablet_id, tserver_ip, snapshot_dir,
                        snapshot_metadata)

                tablet_ids_with_data_dirs.add(tablet_id)

            if tablet_ids_with_data_dirs != tablets_by_tserver_ip[tserver_ip]:
                for possible_tablet_id in tablets_by_tserver_ip[tserver_ip]:
                    if possible_tablet_id not in tablet_ids_with_data_dirs:
                        logging.error(
                            ("No snapshot directory found for tablet id '{}' on "
                             "tablet server '{}'.").format(
                                possible_tablet_id, tserver_ip))
                raise BackupException("Did not find snapshot directories for some tablets on "
                                      + "tablet server " + tserver_ip)

    def get_tmp_dir(self):
        if not self.tmp_dir_name:
            tmp_dir = '/tmp/yb_backup_' + random_string(16)
            atexit.register(self.cleanup_temporary_directory, tmp_dir)
            self.run_program(['mkdir', '-p', tmp_dir])
            self.tmp_dir_name = tmp_dir

        return self.tmp_dir_name

    def upload_metadata_file(self, snapshot_id, snapshot_filepath):
        """
        Generates and uploads a metadata file describing the given snapshot to a file in target
        backup location.
        :param snapshot_id: snapshot id
        :param snapshot_filepath: Backup directory under which to create a path
        """
        metadata_file_path = os.path.join(self.get_tmp_dir(), METADATA_FILE_NAME)

        dest_path = os.path.join(snapshot_filepath, METADATA_FILE_NAME)
        if self.args.local_yb_admin_binary:
            self.run_yb_admin(['export_snapshot', snapshot_id, metadata_file_path])

            if not os.path.exists(metadata_file_path):
                raise BackupException(
                    "Could not find metadata file at '{}'".format(metadata_file_path))

            self.run_program(
                create_checksum_cmd(metadata_file_path, checksum_path(metadata_file_path)))

            self.run_program(
                self.storage.upload_file_cmd(checksum_path(metadata_file_path),
                                             checksum_path(dest_path)))
            self.run_program(
                self.storage.upload_file_cmd(metadata_file_path, dest_path))
        else:
            server_ip = self.get_leader_master_ip()
            self.create_remote_tmp_dir(server_ip)
            self.run_yb_admin(['export_snapshot', snapshot_id, metadata_file_path])

            self.run_ssh_cmd(
                create_checksum_cmd(metadata_file_path, checksum_path(metadata_file_path)),
                server_ip)

            self.run_ssh_cmd(
                self.storage.upload_file_cmd(checksum_path(metadata_file_path),
                                             checksum_path(dest_path)),
                server_ip)

            self.run_ssh_cmd(
                self.storage.upload_file_cmd(metadata_file_path, dest_path),
                server_ip)

        logging.info(
            'Uploaded metadata file %s to %s' % (metadata_file_path, snapshot_filepath))

    def backup_table(self):
        """
        Creates a backup of the given table by creating a snapshot and uploading it to the provided
        backup location.
        """

        if not self.args.table:
            raise BackupException('Need to specify --table')
        if not self.args.keyspace:
            raise BackupException('Need to specify --keyspace')

        logging.info('Backing up tables: {} to {}'.format(self.table_names_str(),
                                                          self.args.backup_location))

        if self.args.snapshot_id:
            logging.info("Using existing snapshot ID: '{}'".format(self.args.snapshot_id))
            snapshot_id = self.args.snapshot_id
        else:
            snapshot_id = self.create_snapshot()
            logging.info("Snapshot started with id: %s" % snapshot_id)
            try:
                self.wait_for_snapshot(snapshot_id, 'creating', CREATE_SNAPSHOT_TIMEOUT_SEC, True)
            except Exception as ex:
                logging.info("Ignoring the exception in the compatibility mode: {}".format(ex))
                # In the compatibility mode repeat the command in old style
                # (without new command line arguments).
                self.wait_for_snapshot(snapshot_id, 'creating', CREATE_SNAPSHOT_TIMEOUT_SEC, False)

        if not self.args.no_snapshot_deleting:
            logging.info("Snapshot %s will be deleted at exit...", snapshot_id)
            atexit.register(self.delete_created_snapshot, snapshot_id)

        tablet_leaders = self.find_tablet_leaders()

        if self.args.no_auto_name:
            snapshot_filepath = self.args.backup_location
        else:
            snapshot_bucket = 'table-{}'.format(self.table_names_str('.', '-'))
            if self.args.table_uuid:
                if len(self.args.table) != len(self.args.table_uuid):
                    raise BackupException(
                        "Found {} --table_uuid keys and {} --table keys. Number of these keys "
                        "must be equal.".format(len(self.args.table_uuid), len(self.args.table)))

                snapshot_bucket = '{}-{}'.format(snapshot_bucket, '-'.join(self.args.table_uuid))

            snapshot_filepath = os.path.join(self.args.backup_location, snapshot_bucket)

        self.upload_metadata_file(snapshot_id, snapshot_filepath)

        self.upload_snapshot_directories(tablet_leaders, snapshot_id, snapshot_filepath)
        logging.info(
            'Backed up tables %s to %s successfully!' %
            (self.table_names_str(), snapshot_filepath))
        print json.dumps({"snapshot_url": snapshot_filepath})

    def download_metadata_file(self):
        """
        Download the metadata file for a backup so as to perform a restore based on it.
        """
        metadata_file_path = os.path.join(self.get_tmp_dir(), METADATA_FILE_NAME)
        src_path = os.path.join(self.args.backup_location, METADATA_FILE_NAME)

        if self.args.local_yb_admin_binary:
            self.run_program(['mkdir', '-p', self.get_tmp_dir()])

            checksum_downloaded = checksum_path_downloaded(metadata_file_path)
            self.run_program(
                self.storage.download_file_cmd(checksum_path(src_path), checksum_downloaded))
            self.run_program(
                self.storage.download_file_cmd(src_path, metadata_file_path))

            self.run_program(
                create_checksum_cmd(metadata_file_path, checksum_path(metadata_file_path)))

            check_checksum_res = self.run_program(
                compare_checksums_cmd(checksum_downloaded,
                                      checksum_path(metadata_file_path))).strip()
        else:
            server_ip = self.get_leader_master_ip()
            self.create_remote_tmp_dir(server_ip)

            checksum_downloaded = checksum_path_downloaded(metadata_file_path)
            self.run_ssh_cmd(
                self.storage.download_file_cmd(checksum_path(src_path), checksum_downloaded),
                server_ip)
            self.run_ssh_cmd(
                self.storage.download_file_cmd(src_path, metadata_file_path),
                server_ip)

            self.run_ssh_cmd(
                create_checksum_cmd(metadata_file_path, checksum_path(metadata_file_path)),
                server_ip)

            check_checksum_res = self.run_ssh_cmd(
                compare_checksums_cmd(checksum_downloaded, checksum_path(metadata_file_path)),
                server_ip).strip()

        if check_checksum_res != 'correct':
            raise BackupException('Check-sum for {} is {}'.format(
                metadata_file_path, check_checksum_res))

        logging.info(
            'Downloaded metadata file %s from %s' % (metadata_file_path, self.args.backup_location))
        return metadata_file_path

    def import_snapshot(self, metadata_file_path):
        """
        Import the snapshot metadata using the provided metadata file, process the metadata for
        the imported snapshot and return the snapshot metadata. The snapshot metadata returned is a
        map containing all the metadata for the snapshot and mappings from old ids to new ids for
        table, keyspace, tablets and snapshot.
        """
        yb_admin_args = ['import_snapshot', metadata_file_path]
        if self.args.table or self.args.keyspace:
            if not self.args.table:
                raise BackupException('Need to specify --table')
            if not self.args.keyspace:
                raise BackupException('Need to specify --keyspace')

            yb_admin_args += self.table_names_str(' ').split(' ')

        output = self.run_yb_admin(yb_admin_args)

        if self.args.verbose:
            logging.info('yb-admin tool output: {}'.format(output))

        snapshot_metadata = {}
        snapshot_metadata['keyspace_name'] = []
        snapshot_metadata['table_name'] = []
        snapshot_metadata['table'] = {}
        snapshot_metadata['tablet'] = {}
        snapshot_metadata['snapshot_id'] = {}
        for line in output.splitlines():
            table_match = IMPORTED_TABLE_RE.search(line)
            if table_match:
                snapshot_metadata['keyspace_name'].append(table_match.group(1))
                snapshot_metadata['table_name'].append(table_match.group(2))
                logging.info('Imported table: {}.{}'.format(table_match.group(1),
                                                            table_match.group(2)))
            if NEW_OLD_UUID_RE.search(line):
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

        return snapshot_metadata

    def find_tablet_replicas(self, snapshot_metadata):
        """
        Finds the tablet replicas for tablets present in snapshot_metadata and returns a list of all
        tservers that need to be processed.
        """

        tablets_by_tserver_ip = {}
        for new_id, old_id in snapshot_metadata['tablet'].iteritems():
            output = self.run_yb_admin(['list_tablet_servers', new_id])
            for line in output.splitlines():
                if LEADING_UUID_RE.match(line):
                    (ts_uuid, ts_ip_port, role) = split_by_tab(line)
                    (ts_ip, ts_port) = ts_ip_port.split(':')
                    tablets_by_tserver_ip.setdefault(ts_ip, set()).add(new_id)

        return tablets_by_tserver_ip

    def identify_new_tablet_replicas(self, tablets_by_tserver_ip_old, tablets_by_tserver_ip_new):
        """
        Compare old and new sets of tablets per every TServer, find and return difference.
        Returns union of the sets per TServer, and delta of the sets.
        """

        tablets_by_tserver_union = copy.deepcopy(tablets_by_tserver_ip_old)
        tablets_by_tserver_delta = {}

        for ip, tablets in tablets_by_tserver_ip_new.iteritems():
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
        pool = ThreadPool(self.args.parallelism)

        tserver_ips = tablets_by_tserver_to_download.keys()
        data_dir_by_tserver = SingleArgParallelCmd(self.find_data_dirs, tserver_ips).run(pool)

        if self.args.verbose:
            logging.info('Found data directories: {}'.format(data_dir_by_tserver))

        (tserver_to_tablet_to_snapshot_dirs, tserver_to_deleted_tablets) =\
            self.generate_snapshot_dirs(
                data_dir_by_tserver, snapshot_id, tablets_by_tserver_to_download, table_ids)

        # Remove deleted tablets from the list of planned to be downloaded tablets.
        for tserver_ip, deleted_tablets in tserver_to_deleted_tablets.iteritems():
            tablets_by_tserver_to_download[tserver_ip] -= deleted_tablets

        parallel_downloads = SequencedParallelCmd(self.run_ssh_cmd)
        self.prepare_cloud_ssh_cmds(
            parallel_downloads, tserver_to_tablet_to_snapshot_dirs, self.args.backup_location,
            snapshot_id, tablets_by_tserver_to_download, upload=False,
            snapshot_metadata=snapshot_meta)

        # Run a sequence of steps for each tablet, handling different tablets in parallel.
        results = parallel_downloads.run(pool)

        for k, v in results.iteritems():
            if v.strip() != 'correct':
                raise BackupException('Check-sum for "{}" is {}'.format(k, v.strip()))

        return tserver_to_deleted_tablets

    def restore_table(self):
        """
        Restore a table from the backup stored in the given backup path.
        """

        logging.info('Restoring backup from {}'.format(self.args.backup_location))

        metadata_file_path = self.download_metadata_file()
        snapshot_metadata = self.import_snapshot(metadata_file_path)
        snapshot_id = snapshot_metadata['snapshot_id']['new']
        table_ids = snapshot_metadata['table'].keys()

        self.wait_for_snapshot(snapshot_id, 'importing', CREATE_SNAPSHOT_TIMEOUT_SEC, False)

        if not self.args.no_snapshot_deleting:
            logging.info("Snapshot %s will be deleted at exit...", snapshot_id)
            atexit.register(self.delete_created_snapshot, snapshot_id)

        all_tablets_by_tserver = self.find_tablet_replicas(snapshot_metadata)
        tablets_by_tserver_to_download = all_tablets_by_tserver

        # The loop must stop after a few rounds because the downloading list includes only new
        # tablets for downloading. The downloading list should become smaller with every round
        # and must become empty in the end.
        while tablets_by_tserver_to_download:
            logging.info(
                'Downloading tablets onto %d tservers...' % (len(tablets_by_tserver_to_download)))

            if self.args.verbose:
                logging.info('Downloading list: {}'.format(tablets_by_tserver_to_download))

            # Download tablets and get list of deleted tablets.
            tserver_to_deleted_tablets = self.download_snapshot_directories(
                snapshot_metadata, tablets_by_tserver_to_download, snapshot_id, table_ids)

            # Remove deleted tablets from the list of all tablets.
            for tserver_ip, deleted_tablets in tserver_to_deleted_tablets.iteritems():
                all_tablets_by_tserver[tserver_ip] -= deleted_tablets

            tablets_by_tserver_new = self.find_tablet_replicas(snapshot_metadata)
            # Calculate the new downloading list as a subtraction of sets:
            #     downloading_list = NEW_all_tablet_replicas - OLD_all_tablet_replicas
            # And extend the list of all tablets (as unioun of sets) for using it on the next
            # loop iteration:
            #     OLD_all_tablet_replicas = OLD_all_tablet_replicas + NEW_all_tablet_replicas
            #                             = OLD_all_tablet_replicas + downloading_list
            (all_tablets_by_tserver, tablets_by_tserver_to_download) =\
                self.identify_new_tablet_replicas(all_tablets_by_tserver, tablets_by_tserver_new)

        # Finally, restore the snapshot.
        logging.info('Downloading is finished. Restoring snapshot %s ...', snapshot_id)
        self.run_yb_admin(['restore_snapshot', snapshot_id])
        self.wait_for_snapshot(snapshot_id, 'restoring', RESTORE_SNAPSHOT_TIMEOUT_SEC, False)

        logging.info('Restored backup successfully!')
        print json.dumps({"success": True})

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

    def run(self):
        try:
            self.post_process_arguments()
            if self.args.command == 'restore':
                self.restore_table()
            elif self.args.command == 'create':
                self.backup_table()
            else:
                logging.error('Command was not specified')
                print json.dumps({"error": "Command was not specified"})
        except BackupException as ex:
            print json.dumps({"error": "Backup exception: {}".format(str(ex))})
        except Exception as ex:
            print json.dumps({"error": "Exception: {}".format(str(ex))})


if __name__ == "__main__":
    YBBackup().run()
