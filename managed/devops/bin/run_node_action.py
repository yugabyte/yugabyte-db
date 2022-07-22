#!/usr/bin/env python

import argparse
from collections import namedtuple
from node_client_utils import KubernetesClient, YB_USERNAME
from pathlib import Path
import sys
import uuid
import warnings
import logging
from ybops.utils.ssh import SSHClient

warnings.filterwarnings("ignore")

NodeTypeParser = namedtuple('NodeTypeParser', ['parser'])
ActionHandler = namedtuple('ActionHandler', ['handler', 'parser'])


def add_k8s_subparser(subparsers, command, parent):
    k8s_parser = subparsers.add_parser(command, help='is k8s universe', parents=[parent])
    k8s_parser.add_argument('--pod_fqdn', type=str, help='k8s pod FQDN', required=True)
    k8s_parser.add_argument('--kubeconfig', type=str, help='k8s kubeconfig', required=True)
    return k8s_parser


def add_ssh_subparser(subparsers, command, parent):
    ssh_parser = subparsers.add_parser(command, help='use ssh (is non k8s universe)',
                                       parents=[parent])
    ssh_parser.add_argument('--key', type=str, help='File auth key for ssh',
                            required=True)
    ssh_parser.add_argument('--ip', type=str, help='IP address for ssh',
                            required=True)
    ssh_parser.add_argument('--port', type=int, help='Port number for ssh', default=22)
    ssh_parser.add_argument('--ssh2_enabled', action='store_true', default=False)
    return ssh_parser


def add_run_command_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='run command and get output',
                                   parents=[parent])
    parser.add_argument('--command', nargs=argparse.REMAINDER)


def handle_run_command(args, client):
    kwargs = {}
    if args.node_type == 'ssh':
        kwargs['output_only'] = True
    output = client.exec_command(args.command, **kwargs)
    print('Command output:')
    print(output)


def add_run_script_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='run script and get output',
                                   parents=[parent])
    parser.add_argument('--local_script_path', type=str,
                        help='Local path to the script to be run')
    parser.add_argument('--params', type=str,
                        help='List of params to pass while calling the script',
                        nargs=argparse.REMAINDER)


def handle_run_script(args, client):
    output = client.exec_script(args.local_script_path, args.params)
    print('Command output:')
    print(output)


def add_download_logs_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='download node logs package',
                                   parents=[parent])
    parser.add_argument('--yb_home_dir', type=str,
                        help='Home directory for YB',
                        default='/home/yugabyte/')
    parser.add_argument('--target_local_file', type=str,
                        help='file to write logs to',
                        required=True)


def download_logs_ssh(args, client):
    # name is irrelevant as long as it doesn't already exist
    tar_file_name = args.node_name + "-support_package.tar.gz"

    cmd = ['tar', '-czvf', tar_file_name, '-h', '-C',
           args.yb_home_dir, 'tserver/logs/yb-tserver.INFO']
    if args.is_master:
        cmd += ['-h', '-C', args.yb_home_dir, 'master/logs/yb-master.INFO']

    rm_cmd = ['rm', tar_file_name]
    client.exec_command(cmd)
    client.download_file_from_remote_server(tar_file_name, args.target_local_file)
    client.exec_command(rm_cmd)


def download_logs_k8s(args, client):
    cmd = ['tar', '-czvf', '-', '-h', '-C',
           args.yb_home_dir]
    if args.is_master:
        cmd += ['master/logs/yb-master.INFO']
    else:
        cmd += ['tserver/logs/yb-tserver.INFO']
    file = open(args.target_local_file, "w+")
    client.get_command_output(cmd, file)


def handle_download_logs(args, client):
    if args.node_type == 'ssh':
        download_logs_ssh(args, client)
    else:
        download_logs_k8s(args, client)


def add_download_file_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='download file',
                                   parents=[parent])
    parser.add_argument('--yb_home_dir', type=str,
                        help='Home directory for YB',
                        default='/home/yugabyte/')
    parser.add_argument('--source_node_files', type=str,
                        help='Source files to download (separated by ;)',
                        required=True)
    parser.add_argument('--target_local_file', type=str,
                        help='Target file to save source to',
                        required=True)


def download_file_ssh(args, client):
    # Name is irrelevant as long as it doesn't already exist
    tar_file_name = args.node_name + "-" + str(uuid.uuid4()) + ".tar.gz"

    # "node_utils.sh/create_tar_file" file takes parameters [home_dir, tar_file_name, file_names...]
    cmd = args.source_node_files.split(";")
    cmd = [file_name for file_name in cmd if file_name.strip() != ""]
    cmd.insert(0, tar_file_name)
    cmd.insert(0, args.yb_home_dir)

    # Execute shell script on remote server and download the file to archive
    script_output = client.exec_script("./bin/node_utils.sh", ["create_tar_file"] + cmd)
    file_exists = client.exec_script("./bin/node_utils.sh",
                                     ["check_file_exists", tar_file_name]).strip()

    print(f"Shell script output : {script_output}")

    check_file_exists_output = int(file_exists)
    if check_file_exists_output:
        rm_cmd = ['rm', tar_file_name]
        client.download_file_from_remote_server(tar_file_name, args.target_local_file)
        client.exec_command(rm_cmd)


def download_file_k8s(args, client):
    # TO DO: Test if k8s works properly!
    # Name is irrelevant as long as it doesn't already exist
    tar_file_name = args.node_name + "-" + str(uuid.uuid4()) + ".tar.gz"

    # "node_utils.sh/create_tar_file" file takes parameters [home_dir, tar_file_name, file_names...]
    cmd = args.source_node_files.split(";")
    cmd = [file_name for file_name in cmd if file_name.strip() != ""]
    cmd.insert(0, tar_file_name)
    cmd.insert(0, args.yb_home_dir)

    # Execute shell script on remote server and download the file to archive
    script_output = client.exec_script("./bin/node_utils.sh", ["create_tar_file"] + cmd)
    print(f"Shell script output : {script_output}")

    # Checking if the file exists
    check_file_exists_output = int(
        client.exec_script("./bin/node_utils.sh", ["check_file_exists", tar_file_name]).strip())
    if check_file_exists_output:
        client.get_file(tar_file_name, args.target_local_file)
        client.exec_command(['rm', tar_file_name])


def handle_download_file(args, client):
    if args.node_type == 'ssh':
        download_file_ssh(args, client)
    else:
        download_file_k8s(args, client)


def add_upload_file_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='upload file',
                                   parents=[parent])
    parser.add_argument('--source_file', type=str,
                        help='Source file path',
                        required=True)
    parser.add_argument('--target_file', type=str,
                        help='Target file path',
                        required=True)
    parser.add_argument('--permissions', type=str,
                        help='Target file permissions',
                        required=True)


def upload_file_ssh(args, client):
    client.upload_file_to_remote_server(args.source_file, args.target_file)


def upload_file_k8s(args, client):
    client.put_file(args.source_file, args.target_file)


def handle_upload_file(args, client):
    logging.info("args here {}".format(args))
    target_path = Path(args.target_file)
    cmd = ['mkdir', '-p', str(target_path.parent.absolute())]
    client.exec_command(cmd)

    if args.node_type == 'ssh':
        upload_file_ssh(args, client)
    else:
        upload_file_k8s(args, client)

    chmod_cmd = ['chmod', args.permissions, args.target_file]
    client.exec_command(chmod_cmd)


node_types = {
    'k8s': NodeTypeParser(add_k8s_subparser),
    'ssh': NodeTypeParser(add_ssh_subparser)
}

actions = {
    'run_command': ActionHandler(handle_run_command, add_run_command_subparser),
    'run_script': ActionHandler(handle_run_script, add_run_script_subparser),
    'download_logs': ActionHandler(handle_download_logs, add_download_logs_subparser),
    'download_file': ActionHandler(handle_download_file, add_download_file_subparser),
    'upload_file': ActionHandler(handle_upload_file, add_upload_file_subparser),
}


def parse_args():
    parent_parser = argparse.ArgumentParser(add_help=False)
    main_parser = argparse.ArgumentParser()
    main_parser.add_argument('--node_name', type=str, help='Node name')
    main_parser.add_argument('--is_master', action='store_true',
                             help='Indicates that node is a master')
    node_type_subparsers = main_parser.add_subparsers(title="node type",
                                                      dest="node_type")
    node_type_subparsers.required = True

    for node_type in node_types:
        node_type_subparser = node_types[node_type]\
          .parser(node_type_subparsers, node_type, parent_parser)
        action_subparser = node_type_subparser.add_subparsers(title="action",
                                                              dest="action")
        action_subparser.required = True
        for action in actions:
            actions[action].parser(action_subparser, action, parent_parser)

    return main_parser.parse_args()


def main():
    args = parse_args()
    if args.node_type == 'ssh':
        client = SSHClient(ssh2_enabled=args.ssh2_enabled)
        try:
            client.connect(args.ip, YB_USERNAME, args.key, args.port)
        except Exception as e:
            sys.exit("Failed to establish SSH connection to {}:{} - {}"
                     .format(args.ip, args.port, str(e)))
    elif args.node_type != 'ssh':
        client = KubernetesClient(args)

    try:
        actions[args.action].handler(args, client)
    finally:
        if args.node_type == 'ssh':
            client.close_connection()


if __name__ == '__main__':
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s: %(message)s")
    main()
