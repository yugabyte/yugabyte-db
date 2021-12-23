#!/usr/bin/env python

import argparse
from collections import namedtuple
from node_client_utils import SshParamikoClient, KubernetesClient

NodeTypeParser = namedtuple('NodeTypeParser', ['parser'])
ActionHandler = namedtuple('ActionHandler', ['handler', 'parser'])


def add_k8s_subparser(subparsers, command, parent):
    k8s_parser = subparsers.add_parser(command, help='is k8s universe', parents=[parent])
    k8s_parser.add_argument('--namespace', type=str, help='k8s namespace', required=True)
    return k8s_parser


def add_ssh_subparser(subparsers, command, parent):
    ssh_parser = subparsers.add_parser(command, help='use ssh (is non k8s universe)',
                                       parents=[parent])
    ssh_parser.add_argument('--key', type=str, help='File auth key for ssh',
                            required=True)
    ssh_parser.add_argument('--ip', type=str, help='IP address for ssh',
                            required=True)
    ssh_parser.add_argument('--port', type=int, help='Port number for ssh', default=22)
    return ssh_parser


def add_run_command_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='run command and get output',
                                   parents=[parent])
    parser.add_argument('--command', type=str, help='Command to run',
                        required=True)


def handle_run_command(args, client):
    output = client.exec_command(args.command)
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

    client.exec_command(cmd)
    sftp_client = client.get_sftp_client()
    sftp_client.get(tar_file_name, args.target_local_file)
    sftp_client.close()

    client.exec_command(['rm', tar_file_name])


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


node_types = {
    'k8s': NodeTypeParser(add_k8s_subparser),
    'ssh': NodeTypeParser(add_ssh_subparser)
}

actions = {
    'run_command': ActionHandler(handle_run_command, add_run_command_subparser),
    'download_logs': ActionHandler(handle_download_logs, add_download_logs_subparser)
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
        client = SshParamikoClient(args)
        client.connect()
    else:
        client = KubernetesClient(args)

    actions[args.action].handler(args, client)

    if args.node_type == 'ssh':
        client.close_connection()


if __name__ == "__main__":
    main()
