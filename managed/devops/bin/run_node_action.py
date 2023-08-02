#!/usr/bin/env python

import argparse
from collections import namedtuple
from node_client_utils import KubernetesClient, YB_USERNAME
from pathlib import Path
import sys
import uuid
import warnings
import logging
import json
import os
from ybops.utils.remote_shell import RemoteShell

warnings.filterwarnings("ignore")

NodeTypeParser = namedtuple('NodeTypeParser', ['parser'])
ActionHandler = namedtuple('ActionHandler', ['handler', 'parser'])


def add_k8s_subparser(subparsers, command, parent):
    k8s_parser = subparsers.add_parser(command, help='is k8s universe', parents=[parent])
    k8s_parser.add_argument('--k8s_config', type=str, help='k8s configuration of a pod',
                            required=True)
    return k8s_parser


def add_ssh_subparser(subparsers, command, parent):
    ssh_parser = subparsers.add_parser(command, help='use ssh (is non k8s universe)',
                                       parents=[parent])
    ssh_parser.add_argument('--key', type=str, help='File auth key for ssh',
                            required=True)
    ssh_parser.add_argument('--ip', type=str, help='IP address for ssh',
                            required=True)
    ssh_parser.add_argument("--user", type=str, help='SSH user', default=YB_USERNAME)
    ssh_parser.add_argument('--port', type=int, help='Port number for ssh', default=22)
    ssh_parser.add_argument('--ssh2_enabled', action='store_true', default=False)
    return ssh_parser


def add_rpc_subparser(subparsers, command, parent):
    rpc_parser = subparsers.add_parser(command, help='use rpc (is non k8s universe)',
                                       parents=[parent])
    rpc_parser.add_argument("--user", type=str, help='RPC user', default=YB_USERNAME)
    rpc_parser.add_argument('--node_agent_ip', type=str, help='IP address for rpc',
                            required=True)
    rpc_parser.add_argument('--node_agent_port', type=int, help='Port number for rpc',
                            required=True)
    rpc_parser.add_argument('--node_agent_cert_path', type=str, help='Path to self-signed cert',
                            required=True)
    rpc_parser.add_argument('--node_agent_auth_token', type=str, help='Auth token for rpc',
                            required=True)
    rpc_parser.add_argument('--node_agent_home', type=str, help='Node agent home path',
                            required=False)
    return rpc_parser


def add_run_command_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='run command and get output',
                                   parents=[parent])
    parser.add_argument('--skip_cmd_logging', action='store_true', default=False)
    parser.add_argument('--command', nargs=argparse.REMAINDER)


def add_test_directory_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command,
                                   help='test directory (ends with \'/\') write permissions',
                                   parents=[parent])
    parser.add_argument('--test_directory', type=str, help='Path of directory to test',
                        required=True)
    return parser


def handle_run_command(args, client):
    kwargs = {}
    if args.node_type == 'ssh' or args.node_type == 'rpc':
        kwargs['output_only'] = True
        if args.skip_cmd_logging:
            kwargs['skip_cmd_logging'] = True
    output = client.exec_command(args.command, **kwargs)
    print('Command output:')
    print(output)


def handle_test_directory(args, client):
    filename = args.test_directory + str(uuid.uuid4())
    client.exec_command('touch ' + filename)
    client.exec_command('echo "This is some text" > ' + filename)
    output = client.exec_command('cat ' + filename)
    client.exec_command('rm ' + filename)
    if 'This is some text' in str(output):
        print('Directory is writable')
    else:
        print('Directory is not writable')


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
    client.fetch_file(tar_file_name, args.target_local_file)
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
    if args.node_type == 'ssh' or args.node_type == 'rpc':
        download_logs_ssh(args, client)
    else:
        download_logs_k8s(args, client)


def add_download_file_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='download file',
                                   parents=[parent])
    parser.add_argument('--yb_home_dir', type=str,
                        help='Home directory for YB',
                        default='/home/yugabyte/')
    parser.add_argument('--source_node_files_path', type=str,
                        help='File containing source files to download (separated by new line)',
                        required=True)
    parser.add_argument('--target_local_file', type=str,
                        help='Target file to save source to',
                        required=True)


def download_file_node(args, client):
    # Name is irrelevant as long as it doesn't already exist
    tar_file_name = args.node_name + "-" + str(uuid.uuid4()) + ".tar.gz"
    target_node_files_path = args.source_node_files_path

    # "node_utils.sh/create_tar_file" file takes parameters:
    # [home_dir, tar_file_name, file_list_text_path]
    cmd = [args.source_node_files_path]
    client.put_file(args.source_node_files_path, target_node_files_path)
    cmd.insert(0, tar_file_name)
    cmd.insert(0, args.yb_home_dir)

    # Execute shell script on remote server and download the file to archive
    script_output = client.exec_script("./bin/node_utils.sh", ["create_tar_file"] + cmd)
    file_exists = client.exec_script("./bin/node_utils.sh",
                                     ["check_file_exists", tar_file_name]).strip()

    print(f"Shell script output : {script_output}")

    check_file_exists_output = int(file_exists)
    if check_file_exists_output:
        rm_cmd = ['rm', tar_file_name, target_node_files_path]
        client.fetch_file(tar_file_name, args.target_local_file)
        client.exec_command(rm_cmd)


def download_file_k8s(args, client):
    # TO DO: Test if k8s works properly!
    # Name is irrelevant as long as it doesn't already exist
    tar_file_name = args.node_name + "-" + str(uuid.uuid4()) + ".tar.gz"
    target_node_files_path = args.source_node_files_path

    # "node_utils.sh/create_tar_file" file takes parameters:
    # [home_dir, tar_file_name, file_list_text_path]
    cmd = [args.source_node_files_path]
    client.put_file(args.source_node_files_path, target_node_files_path)
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
        client.exec_command(['rm', tar_file_name, target_node_files_path])


def handle_download_file(args, client):
    if args.node_type == 'ssh' or args.node_type == 'rpc':
        download_file_node(args, client)
    else:
        download_file_k8s(args, client)


def add_copy_file_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='copy file from remote to local',
                                   parents=[parent])
    parser.add_argument('--remote_file', type=str,
                        help='Source file path',
                        required=True)
    parser.add_argument('--local_file', type=str,
                        help='Target file path',
                        required=True)


def copy_file_ssh(args, client):
    client.fetch_file(args.remote_file, args.local_file)


def copy_file_k8s(args, client):
    client.get_file(args.remote_file, args.local_file)


def handle_copy_file(args, client):
    local_path = Path(args.local_file)
    cmd = ['mkdir', '-p', str(local_path.parent.absolute())]
    client.exec_command(cmd)

    if args.node_type == 'ssh' or args.node_type == 'rpc':
        copy_file_ssh(args, client)
    else:
        copy_file_k8s(args, client)


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
    client.put_file(args.source_file, args.target_file)


def upload_file_k8s(args, client):
    client.put_file(args.source_file, args.target_file)


def handle_upload_file(args, client):
    target_path = Path(args.target_file)
    cmd = ['mkdir', '-p', str(target_path.parent.absolute())]
    client.exec_command(cmd)

    if args.node_type == 'ssh' or args.node_type == 'rpc':
        upload_file_ssh(args, client)
    else:
        upload_file_k8s(args, client)

    chmod_cmd = ['chmod', args.permissions, args.target_file]
    client.exec_command(chmod_cmd)


def add_bulk_check_files_exist_subparser(subparsers, command, parent):
    parser = subparsers.add_parser(command, help='bulk check files exist',
                                   parents=[parent])
    parser.add_argument('--source_files_to_check_path', type=str,
                        help='Local File containing file paths to check existence of',
                        required=True)
    parser.add_argument('--yb_dir', type=str,
                        help='Target directory to use for file upload/download',
                        required=True)
    parser.add_argument('--target_local_file_path', type=str,
                        help='Target local file path to save file_check output',
                        required=True)


def handle_bulk_check_files_exist(args, client):
    if args.node_type == 'ssh' or args.node_type == 'rpc':
        bulk_check_files_exist_node(args, client)
    else:
        bulk_check_files_exist_k8s(args, client)


def bulk_check_files_exist_node(args, client):
    uuid_str = str(uuid.uuid4())
    # Upload file containing paths to check existence of to node
    target_files_to_check_name = "bulk_check_files" + "-" + uuid_str
    target_files_to_check_path = os.path.join(args.yb_dir, target_files_to_check_name)
    client.put_file(args.source_files_to_check_path, target_files_to_check_path)

    # Execute shell script on remote server and download script output file
    file_check_output_filename = "bulk_check_files_output" + "-" + uuid_str
    file_check_output_filepath = os.path.join(args.yb_dir, file_check_output_filename)
    cmd = [file_check_output_filename]
    cmd.insert(0, args.yb_dir)
    cmd.insert(0, target_files_to_check_path)
    script_output = client.exec_script("./bin/node_utils.sh", ["bulk_check_files_exist"] + cmd)
    file_exists = client.exec_script("./bin/node_utils.sh",
                                     ["check_file_exists",
                                      file_check_output_filepath]).strip()

    print(f"Shell script output : {script_output}")

    check_file_exists_output = int(file_exists)
    if check_file_exists_output:
        rm_cmd = ['rm', target_files_to_check_path, file_check_output_filepath]
        client.fetch_file(file_check_output_filepath, args.target_local_file_path)
        client.exec_command(rm_cmd)


def bulk_check_files_exist_k8s(args, client):
    uuid_str = str(uuid.uuid4())
    # Upload file containing paths to check existence of to node
    target_files_to_check_name = "bulk_check_files" + "-" + uuid_str
    target_files_to_check_path = os.path.join(args.yb_dir, target_files_to_check_name)
    client.put_file(args.source_files_to_check_path, target_files_to_check_path)

    # Execute shell script on remote server and download script output file
    file_check_output_filename = "bulk_check_files_output" + "-" + uuid_str
    file_check_output_filepath = os.path.join(args.yb_dir, file_check_output_filename)
    cmd = [file_check_output_filename]
    cmd.insert(0, args.yb_dir)
    cmd.insert(0, target_files_to_check_path)
    script_output = client.exec_script("./bin/node_utils.sh", ["bulk_check_files_exist"] + cmd)
    file_exists = client.exec_script("./bin/node_utils.sh",
                                     ["check_file_exists",
                                      file_check_output_filepath]).strip()

    print(f"Shell script output : {script_output}")

    check_file_exists_output = int(file_exists)
    if check_file_exists_output:
        rm_cmd = ['rm', target_files_to_check_path, file_check_output_filepath]
        client.get_file(file_check_output_filepath, args.target_local_file_path)
        client.exec_command(rm_cmd)


node_types = {
    'k8s': NodeTypeParser(add_k8s_subparser),
    'ssh': NodeTypeParser(add_ssh_subparser),
    'rpc': NodeTypeParser(add_rpc_subparser)
}

actions = {
    'run_command': ActionHandler(handle_run_command, add_run_command_subparser),
    'run_script': ActionHandler(handle_run_script, add_run_script_subparser),
    'download_logs': ActionHandler(handle_download_logs, add_download_logs_subparser),
    'download_file': ActionHandler(handle_download_file, add_download_file_subparser),
    'copy_file': ActionHandler(handle_copy_file, add_copy_file_subparser),
    'upload_file': ActionHandler(handle_upload_file, add_upload_file_subparser),
    'test_directory': ActionHandler(handle_test_directory, add_test_directory_subparser),
    'bulk_check_files_exist': ActionHandler(handle_bulk_check_files_exist,
                                            add_bulk_check_files_exist_subparser)
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
        ssh_options = {
            "connection_type": "ssh",
            "ssh_user": YB_USERNAME if args.user is None else args.user,
            "ssh_host": args.ip,
            "ssh_port": args.port,
            "private_key_file": args.key,
            "ssh2_enabled": args.ssh2_enabled
        }
        logging.info("Using SSH connection to {}:{}".format(args.ip, args.port))
        try:
            client = RemoteShell(ssh_options)
        except Exception as e:
            sys.exit("Failed to establish SSH connection to {}:{} - {}"
                     .format(args.ip, args.port, str(e)))
    elif args.node_type == 'rpc':
        rpc_options = {
            "connection_type": "node_agent_rpc",
            "node_agent_user":  YB_USERNAME if args.user is None else args.user,
            "node_agent_ip": args.node_agent_ip,
            "node_agent_port": args.node_agent_port,
            "node_agent_cert_path": args.node_agent_cert_path,
            "node_agent_auth_token": args.node_agent_auth_token
        }
        logging.info("Using RPC connection to {}:{}"
                     .format(args.node_agent_ip, args.node_agent_port))
        try:
            client = RemoteShell(rpc_options)
        except Exception as e:
            sys.exit("Failed to establish RPC connection to {}:{} - {}"
                     .format(args.node_agent_ip, args.node_agent_port, str(e)))
    else:
        args.k8s_config = json.loads(args.k8s_config)
        if args.k8s_config is None:
            sys.exit("Failed to load k8s configs")
        client = KubernetesClient(args)

    try:
        actions[args.action].handler(args, client)
    finally:
        if args.node_type == 'ssh' or args.node_type == 'rpc':
            client.close()


if __name__ == '__main__':
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s: %(message)s")
    main()
