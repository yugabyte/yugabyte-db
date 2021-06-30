import subprocess
import argparse
import paramiko
from collections import namedtuple

YB_USERNAME = 'yugabyte'


CommandHandler = namedtuple('CommandHandler', ['handler', 'parser'])


class KubernetesClient:
    def __init__(self, args):
        self.namespace = args.namespace
        self.node_name = args.node_name
        self.yb_home_dir = args.yb_home_dir
        self.is_master = args.is_master

    def exec_command(self, cmd, stdout=None):
        cmd = ['kubectl', 'exec', '-n', self.namespace, '-c',
               'yb-master' if self.is_master else 'yb-tserver', self.node_name, '--'] + cmd
        return subprocess.call(cmd, stdout=stdout)


class SshParamikoClient:
    def __init__(self, args):
        self.key_filename = args.key
        self.ip = args.ip
        self.port = args.port
        self.yb_home_dir = args.yb_home_dir
        self.node_name = args.node_name
        self.client = None

    def connect(self):
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.client.load_system_host_keys()
        self.client.connect(self.ip, self.port, username=YB_USERNAME,
                            key_filename=self.key_filename)

    def close_connection(self):
        self.client.close()

    def get_remote_env_var(self, env):
        _, stdout, _ = self.client.exec_command('echo ${}'.format(env))
        try:
            var = stdout.read()[:-1].decode()  # decode bytes to string
        except Exception:
            raise RuntimeError("Env var {} does not exist".format(env))
        return var

    def get_sftp_client(self):
        return self.client.open_sftp()

    def exec_command(self, cmd):
        return self.client.exec_command(' '.join(cmd))[1].read().decode()


def add_k8s_subparser(subparsers, subcommand_name):
    k8s_parser = subparsers.add_parser(subcommand_name, help='is k8s universe')
    k8s_parser.add_argument('--namespace', type=str, help='k8s namespace', required=True)
    k8s_parser.add_argument('--node_name', type=str, help='Node name', required=True)
    k8s_parser.add_argument('--yb_home_dir', type=str, help='Home directory for YB',
                            default='/home/yugabyte/')
    k8s_parser.add_argument(
        '--is_master',
        action='store_true',
        help='Indicates that node is a master')
    k8s_parser.add_argument(
        '--target_local_file',
        type=str,
        help='file to write logs to',
        required=True)


def add_ssh_subparser(subparsers, subcommand_name):
    ssh_parser = subparsers.add_parser(subcommand_name, help='use ssh (is non k8s universe)')
    ssh_parser.add_argument('--key', type=str, help='File auth key for ssh',
                            required=True)
    ssh_parser.add_argument('--node_name', type=str, help='Node name', required=True)

    ssh_parser.add_argument('--ip', type=str, help='IP address for ssh',
                            required=True)
    ssh_parser.add_argument('--port', type=int, help='Port number for ssh')
    ssh_parser.add_argument('--yb_home_dir', type=str,
                            help='Home directory for YB',
                            default='/home/yugabyte/')
    ssh_parser.add_argument('--is_master', action='store_true',
                            help='Indicates that node is a master')
    ssh_parser.add_argument(
        '--target_local_file',
        type=str,
        help='file to write logs to',
        required=True)


def handle_ssh_universe(args):
    client = SshParamikoClient(args)
    client.connect()

    # name is irrelevant as long as it doesn't already exist
    tar_file_name = client.node_name + "-support_package.tar.gz"

    cmd = ['tar', '-czvf', tar_file_name, '-h', '-C',
           args.yb_home_dir, 'tserver/logs/yb-tserver.INFO']
    if args.is_master:
        cmd += ['-h', '-C', args.yb_home_dir, 'master/logs/yb-master.INFO']

    client.exec_command(cmd)
    sftp_client = client.get_sftp_client()
    sftp_client.get(tar_file_name, args.target_local_file)
    sftp_client.close()

    client.exec_command(['rm', tar_file_name])
    client.close_connection()


def handle_k8s_universe(args):
    client = KubernetesClient(args)
    cmd = ['tar', '-czvf', '-', '-h', '-C',
           args.yb_home_dir]
    if args.is_master:
        cmd += ['master/logs/yb-master.INFO']
    else:
        cmd += ['tserver/logs/yb-tserver.INFO']
    file = open(args.target_local_file, "w+")
    client.exec_command(cmd, file)
