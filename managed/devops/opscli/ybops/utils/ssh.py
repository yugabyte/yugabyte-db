#!/usr/bin/env python
#
# Copyright 2022 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import datetime
import functools
import logging
import os
import paramiko
import pipes
import shutil
import socket
import stat
import subprocess
import time
import tempfile

from Crypto.PublicKey import RSA

from ybops.common.exceptions import YBOpsRuntimeError, YBOpsRecoverableError

SSH2 = 'ssh2'
SSH = 'ssh'
SSH_RETRY_LIMIT = 60
SSH_RETRY_LIMIT_PRECHECK = 4
DEFAULT_SSH_PORT = 22
DEFAULT_SSH_USER = 'centos'
# Timeout in seconds.
SSH_TIMEOUT = 45
# Retry in seconds
SSH_RETRY_DELAY = 10
RSA_KEY_LENGTH = 2048
CONNECTION_RETRY_DELAY_SEC = 15
# Let's set some timeout to our commands.
# If 10 minutes will not be enough for something - will have to pass command timeout as an argument.
# Just having timeout in shell script, which we're running on the node,
# does not seem to always help - as ssh client connection itself or command results read can hang.
COMMAND_TIMEOUT_SEC = 600


def ssh_retry_decorator(fn_to_call, exc_handler=None, retry_delay=None):
    if fn_to_call is None:
        return functools.partial(
            ssh_retry_decorator, exc_handler=exc_handler, retry_delay=retry_delay)

    @functools.wraps(fn_to_call)
    def wrapper(*args, **kwargs):
        max_attempts = 3

        for i in range(1, max_attempts + 1):
            try:
                return fn_to_call(*args, **kwargs)
            except Exception as e:
                if i < max_attempts and exc_handler(e):
                    time.sleep(retry_delay)
                    continue
                raise YBOpsRecoverableError(str(e))

    return wrapper


def ssh_exception_handler(e):
    if isinstance(e, (paramiko.SSHException, socket.error)):
        logging.warning('Caught SSH error %s, retrying: %s', type(e).__name__, e)
        return True
    return False


def retry_ssh_errors(fn=None, retry_delay=SSH_RETRY_DELAY):
    return ssh_retry_decorator(fn, exc_handler=ssh_exception_handler, retry_delay=retry_delay)


def parse_private_key(key):
    """Parses the private key file, & returns
    the underlying format that the key uses.
    :param key: private key file.
    :return: Private key type(One of SSH2/SSH).
    """
    if key is None:
        raise YBOpsRuntimeError("Private key file not specified. Returning.")

    with open(key) as f:
        key_data = f.read()
        try:
            RSA.importKey(key_data)
            return SSH
        except ValueError:
            '''
            SSH2 encrypted keys contains Subject & comment in the generated body.
            '---- BEGIN SSH2 ENCRYPTED PRIVATE KEY ----'
            'Subject: user'
            'Comment: "2048-bit rsa'
            '''
            key_val = key_data.split('\n')
            if 'Subject' in key_val[1] and 'Comment' in key_val[2]:
                return SSH2

    logging.info("[app], specified key format is not supported.")
    raise YBOpsRuntimeError("Specified key format is not supported.")


def check_ssh2_bin_present():
    """Checks if the ssh2 is installed on the node
    :return: True/False
    """
    try:
        output = run_command(['command', '-v', '/usr/bin/sshg3', '/dev/null'])
        return True if output is not None else False
    except YBOpsRuntimeError:
        return False


def run_command(args, num_retry=1, timeout=1, **kwargs):
    cmd_as_str = quote_cmd_line_for_bash(args)
    logging.info("[app] Executing command \"{}\"".format(cmd_as_str))
    while num_retry > 0:
        num_retry = num_retry - 1
        try:
            process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            output, err = process.communicate()
            if process.returncode != 0:
                logging.error("Failed to run command [[ {} ]]: code={} output={}".format(
                  cmd_as_str, process.returncode, err))
                raise YBOpsRuntimeError(err.decode('utf-8'))
            return output.decode('utf-8')

        except Exception as ex:
            logging.error("Failed to run command [[ {} ]]: {}".format(cmd_as_str, ex))
            sleep_or_raise(num_retry, timeout, ex)


def quote_cmd_line_for_bash(cmd_line):
    if not isinstance(cmd_line, list) and not isinstance(cmd_line, tuple):
        raise Exception("Expected a list/tuple, got: [[ {} ]]".format(cmd_line))
    return ' '.join([pipes.quote(str(arg)) for arg in cmd_line])


def sleep_or_raise(num_retry, timeout, ex):
    if num_retry > 0:
        logging.info("Sleep {}... ({} retries left)".format(timeout, num_retry))
        time.sleep(timeout)
    else:
        raise ex


def can_ssh(host_name, port, username, ssh_key_file, **kwargs):
    """This method tries to ssh to the host with the username provided on the port.
    and returns if ssh was successful or not.
    Args:
        host_name (str): SSH host IP address
        port (int): SSH port
        username (str): SSH username
        ssh_key_file (str): SSH key file
    Returns:
        (boolean): If SSH was successful or not.
    """
    try:
        ssh2_enabled = kwargs.get('ssh2_enabled', False)
        ssh_client = SSHClient(ssh2_enabled=ssh2_enabled)
        ssh_client.connect(host_name, username, ssh_key_file, port)
        stdout = ssh_client.exec_command("echo 'test'", output_only=True)
        stdout = stdout.splitlines()
        if len(stdout) == 1 and (stdout[0] == "test"):
            return True
        return False
    except Exception as e:
        logging.error("Error Checking the instance, {}".format(e))
        return False


def wait_for_ssh(host_ip, ssh_port, ssh_user, ssh_key, num_retries=SSH_RETRY_LIMIT, **kwargs):
    """This method would basically wait for the given host's ssh to come up, by looping
    and checking if the ssh is active. And timesout if retries reaches num_retries.
    Args:
        host_ip (str): IP Address for which we want to ssh
        ssh_port (str): ssh port
        ssh_user (str): ssh user name
        ssh_key (str): ssh key filename
    Returns:
        (boolean): Returns true if the ssh was successful.
    """
    retry_count = 0
    while retry_count < num_retries:
        if can_ssh(host_ip, ssh_port, ssh_user, ssh_key, **kwargs):
            return True

        time.sleep(1)
        retry_count += 1

    return False


def format_rsa_key(key, public_key=False):
    """This method would take the rsa key and format it based on whether it is
    public key or private key.
    Args:
        key (RSA Key): Key data
        public_key (bool): Denotes if we need public key or not.
    Returns:
        key (str): Encoded key in OpenSSH or PEM format based on the flag (public key or not).
    """
    if isinstance(key, RSA.RsaKey):
        if public_key:
            return key.publickey().exportKey("OpenSSH").decode('utf-8')
        return key.exportKey("PEM").decode('utf-8')
    else:
        if public_key:
            run_command(['ssh-keygen-g3', '-D', key])
            file = key + '.pub'
            p_key = None
            with open(file) as f:
                p_key = f.read()
            logging.info("generating public key, {}".format(p_key))

            return p_key
        else:
            with open(key) as f:
                return f.read()


def validated_key_file(key_file):
    """This method would validate a given key file and raise a exception if the file format
    is incorrect or not found.
    Args:
        key_file (str): Key file name
        public_key (bool): Denote if the key file is public key or not.
    Returns:
        key (RSA Key): RSA key data
    """

    if not os.path.exists(key_file):
        raise YBOpsRuntimeError("Key file {} not found.".format(key_file))

    # Check based on key_type not on SSH2 installed or not.
    ssh_type = parse_private_key(key_file)
    if ssh_type == SSH:
        with open(key_file) as f:
            return RSA.importKey(f.read())
    else:
        return key_file


def generate_rsa_keypair(key_name, destination='/tmp'):
    """This method would generate a RSA Keypair with an exponent of 65537 in PEM format,
    We will also make the files once generated READONLY by owner, this is need for SSH
    to work.
    Args:
        key_name(str): Keypair name
        destination (str): Destination folder
    Returns:
        keys (tuple): Private and Public key files.
    """
    new_key = RSA.generate(RSA_KEY_LENGTH)
    if not os.path.exists(destination):
        raise YBOpsRuntimeError("Destination folder {} not accessible".format(destination))

    public_key_filename = os.path.join(destination, "{}.pub".format(key_name))
    private_key_filename = os.path.join(destination, "{}.pem".format(key_name))
    if os.path.exists(public_key_filename):
        raise YBOpsRuntimeError("Public key file {} already exists".format(public_key_filename))
    if os.path.exists(private_key_filename):
        raise YBOpsRuntimeError("Private key file {} already exists".format(private_key_filename))

    with open(public_key_filename, "w") as f:
        f.write(format_rsa_key(new_key, public_key=True))
        os.chmod(f.name, stat.S_IRUSR)
    with open(private_key_filename, "w") as f:
        f.write(format_rsa_key(new_key, public_key=False))
        os.chmod(f.name, stat.S_IRUSR)

    return private_key_filename, public_key_filename


def scp_to_tmp(filepath, host, user, port, private_key, retries=3,
               retry_delay=SSH_RETRY_DELAY, **kwargs):
    dest_path = os.path.join("/tmp", os.path.basename(filepath))
    logging.info("[app] Copying local '{}' to remote '{}'".format(
        filepath, dest_path))
    ssh2_enabled = kwargs.get('ssh2_enabled', False)
    ssh_key_flag = '-K' if ssh2_enabled else '-i'
    scp = 'scpg3' if ssh2_enabled else 'scp'
    scp_cmd = [
        scp, ssh_key_flag, private_key, "-P", str(port), "-p",
        "-o", "stricthostkeychecking=no",
        "-o", "ServerAliveInterval=30",
        "-o", "ServerAliveCountMax=20",
        "-o", "ControlMaster=auto",
        "-o", "ControlPersist=600s",
        "-o", "IPQoS=throughput",
        "-vvvv",
        filepath, "{}@{}:{}".format(user, host, dest_path)
    ]

    rc = 0
    while retries > 0:
        # Save the debug output to temp files.
        out_fd, out_name = tempfile.mkstemp(text=True)
        err_fd, err_name = tempfile.mkstemp(text=True)
        # Start the scp and redirect out and err.
        proc = subprocess.Popen(scp_cmd, stdout=out_fd, stderr=err_fd)
        # Wait for finish and cleanup FDs.
        proc.wait()
        os.close(out_fd)
        os.close(err_fd)
        rc = proc.returncode

        # In case of errors, copy over the tmp output.
        if rc != 0:
            timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
            basename = f"/tmp/{host}-{timestamp}"
            logging.warning(f"Command '{' '.join(scp_cmd)}' failed with exit code {rc}")

            for ext, name in {'out': out_name, 'err': err_name}.items():
                logging.warning(f"Dumping std{ext} to {basename}.{ext}")
                shutil.move(name, f"{basename}.out")

            retries -= 1
            time.sleep(retry_delay)
        else:
            # Cleanup the temp files now that they are clearly not needed.
            os.remove(out_name)
            os.remove(err_name)
            break

    return rc


def get_public_key_content(private_key_file):
    rsa_key = validated_key_file(private_key_file)
    public_key_content = format_rsa_key(rsa_key, public_key=True)
    return public_key_content


def get_ssh_host_port(host_info, custom_port, default_port=False):
    """This method would return ssh_host and port which we should use for ansible. If host_info
    includes a ssh_port key, then we return its value. Otherwise, if the default_port param is
    True, then we return a Default SSH port (22) else, we return a custom ssh port.
    Args:
        host_info (dict): host_info dictionary that we fetched from inventory script, we
                          fetch the private_ip from that.
        default_port(boolean): Boolean to denote if we want to use default ssh port or not.
    Returns:
        (dict): a dictionary with ssh_port and ssh_host data.
    """
    ssh_port = host_info.get("ssh_port")
    if ssh_port is None:
        ssh_port = (DEFAULT_SSH_PORT if default_port else custom_port)
    return {
        "ssh_port": ssh_port,
        "ssh_host": host_info["private_ip"]
    }


class SSHClient(object):
    '''
        Base SSH Class Library. Handles invoking the paramiko in case
        the OpenSSH keys are used for initialization,
        invokes native ssh command otherwise.
    '''

    def __init__(self, ssh2_enabled=False):
        self.hostname = ''
        self.username = ''
        self.key = None
        self.port = ''
        self.client = None
        self.sftp_client = None
        self.ssh_type = SSH2 if ssh2_enabled and check_ssh2_bin_present() else SSH

    @retry_ssh_errors(retry_delay=CONNECTION_RETRY_DELAY_SEC)
    def connect(self, hostname, username, key, port, retry=3, timeout=SSH_TIMEOUT):
        '''
            Initializes the connection or stores the relevant information
            needed for performing native ssh.
        '''
        if self.ssh_type == SSH:
            ssh_key = paramiko.RSAKey.from_private_key_file(key)

            try:
                self.client = paramiko.SSHClient()
                self.client.set_missing_host_key_policy(paramiko.MissingHostKeyPolicy())
                self.client.connect(
                    hostname=hostname,
                    username=username,
                    pkey=ssh_key,
                    port=port,
                    timeout=timeout,
                    banner_timeout=timeout
                )
            except socket.error as ex:
                logging.error("[app] Failed to establish SSH connection to {}:{} - {}"
                              .format(hostname, port, str(ex)))
                raise ex
        else:
            self.hostname = hostname
            self.username = username
            self.key = key
            self.port = port

    @retry_ssh_errors
    def exec_command(self, cmd, **kwargs):
        '''
            Executes the command on the remote machine.
            If `output_only` kwargs is passed, then only output
            is returned to the client. Else we return stdin, stdout, stderr.
        '''
        output_only = kwargs.get('output_only', False)
        skip_cmd_logging = kwargs.get('skip_cmd_logging', False)
        if isinstance(cmd, str):
            command = cmd
        else:
            # Need to join with spaces, but surround arguments with spaces using "'" character
            command = ' '.join(
                list(map(lambda part: part if ' ' not in part else "'" + part + "'", cmd)))
        if self.ssh_type == SSH:
            if not skip_cmd_logging:
                logging.info("Executing command {}".format(command))
            _, stdout, stderr = self.client.exec_command(command)
            if not output_only:
                output = stdout.readlines()
                error = stderr.readlines()
                return stdout.channel.recv_exit_status(), output, error
            else:
                output = self.read_output(stdout)
                error = self.read_output(stderr)
                # We should read output first before we call recv_exit_status()
                # see https://github.com/paramiko/paramiko/issues/448
                return_code = stdout.channel.recv_exit_status()
                if return_code != 0:
                    raise YBOpsRuntimeError('Command \'{}\' returned error code {}: {}'.format(
                        "" if skip_cmd_logging else command, return_code, error))
                return output
        else:
            cmd = self.__generate_shell_command(
                self.hostname,
                self.port, self.username,
                self.key, command=command)
            output = run_command(cmd)
            if not output_only:
                return 0, output, None
            else:
                return output

    def close_connection(self):
        '''
            Closes the remote connection, for paramiko
            client opened for openssh connection.
        '''
        if self.ssh_type == SSH:
            self.client.close()

    def read_output(self, stream):
        '''
        We saw this script hang. The only place which can hang in theory is ssh command execution
        and reading it's results.
        Applied one of described workaround from this issue:
        https://github.com/paramiko/paramiko/issues/109
        '''
        end_time = time.time() + COMMAND_TIMEOUT_SEC
        while not stream.channel.eof_received:
            time.sleep(1)
            if time.time() > end_time:
                stream.channel.close()
            break
        return stream.read().decode()

    @retry_ssh_errors
    def exec_script(self, local_script_name, params):
        '''
        Function to execute a local bash script on the remote ssh server.
        Parameters:
        local_script_name : Path to the shell script on local machine
        params: List of arguments to be provided to the shell script
        '''
        if not isinstance(params, str):
            params = ' '.join(params)

        with open(local_script_name, "r") as f:
            local_script = f.read()

        # Heredoc syntax for input redirection from a local shell script
        command = f"/bin/bash -s {params} <<'EOF'\n{local_script}\nEOF"
        stdout = self.exec_command(command, output_only=True)

        return stdout

    @retry_ssh_errors
    def download_file_from_remote_server(self, remote_file_name, local_file_name):
        '''
            Function to download a file from remote server on the local machine.
            Parameters:
            local_file_name : Path to the shell script on local machine
            remote_file_name: Path to the shell script on remote machine
        '''
        if self.ssh_type == SSH:
            self.sftp_client = self.client.open_sftp()
            try:
                self.sftp_client.get(remote_file_name, local_file_name)
            finally:
                self.sftp_client.close()
        else:
            cmd = self.__generate_shell_command(self.hostname, self.port,
                                                self.username, self.key,
                                                src_filepath=remote_file_name,
                                                dest_filepath=local_file_name,
                                                get_from_remote=True)
            run_command(cmd)

    @retry_ssh_errors
    def upload_file_to_remote_server(self, local_file_name, remote_file_name, **kwargs):
        '''
            Function to upload a file from local server on the remote machine.
            Parameters:
            local_file_name : Path to the shell script on local machine
            remote_file_name: Path to the shell script on remote machine
        '''
        if self.ssh_type == SSH:
            self.sftp_client = self.client.open_sftp()
            try:
                self.sftp_client.put(local_file_name, remote_file_name)
                chmod = kwargs.get('chmod', 0)
                if chmod != 0:
                    self.sftp_client.chmod(remote_file_name, chmod)
            except Exception as e:
                logging.warning('Caught exception on file transfer', e)
                raise e
            finally:
                self.sftp_client.close()
        else:
            cmd = self.__generate_shell_command(self.hostname, self.port,
                                                self.username, self.key,
                                                src_filepath=local_file_name,
                                                dest_filepath=remote_file_name)
            run_command(cmd)

    def __generate_shell_command(self, host_name, port, username, ssh_key_file, **kwargs):
        '''
            This method generates & returns the actual shell command,
            that will be executed as subprocess.
        '''
        # The flag on which we specify the private_key_file differs in
        # ssh version. In SSH it is specified via `-i` will in SSH2 via `-K`
        extra_commands = kwargs.get('extra_commands', [])
        command = kwargs.get('command', None)
        src_filepath = kwargs.get('src_filepath', None)
        dest_filepath = kwargs.get('dest_filepath', None)
        is_file_download = kwargs.get('get_from_remote', False)

        ssh_key_flag = '-i' if self.ssh_type == SSH else '-K'
        cmd = []

        if not src_filepath and not dest_filepath:
            cmd = ['ssh'] if self.ssh_type == SSH else ['sshg3']
            cmd += ['-p', str(port)]
        else:
            cmd = ['scp'] if self.ssh_type == SSH else ['scpg3']
            cmd += ['-P', str(port)]

        if len(extra_commands) != 0:
            cmd += extra_commands

        cmd.extend([
            '-o', 'StrictHostKeyChecking=no',
            ssh_key_flag, ssh_key_file,
        ])

        if not src_filepath and not dest_filepath:
            cmd.extend([
                '%s@%s' % (username, host_name)
            ])
            if isinstance(command, list):
                cmd += command
            else:
                cmd.append(command)
        else:
            if not is_file_download:
                cmd.extend([
                    src_filepath,
                    '%s@%s:%s' % (username, host_name, dest_filepath)
                ])
            else:
                cmd.extend([
                    '%s@%s:%s' % (username, host_name, src_filepath),
                    dest_filepath
                ])
        return cmd
