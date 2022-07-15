import os
import paramiko
import socket
import subprocess
import time
import logging

YB_USERNAME = 'yugabyte'

CONNECTION_RETRY_COUNT = 3
CONNECTION_RETRY_DELAY_SEC = 15
# Let's set some timeout to our commands.
# If 10 minutes will not be enough for something - will have to pass command timeout as an argument.
# Just having timeout in shell script, which we're running on the node,
# does not seem to always help - as ssh client connection itself or command results read can hang.
COMMAND_TIMEOUT_SEC = 600


class KubernetesClient:
    def __init__(self, args):
        self.namespace = args.pod_fqdn.split('.')[2]
        self.pod_name = args.pod_fqdn.split('.')[0]
        self.is_master = args.is_master
        self.env_config = os.environ.copy()
        self.env_config["KUBECONFIG"] = args.kubeconfig

    def wrap_command(self, cmd):
        command = cmd
        if isinstance(cmd, str):
            command = [cmd]
        return ['kubectl', 'exec', '-n', self.namespace, '-c',
                'yb-master' if self.is_master else 'yb-tserver', self.pod_name, '--'] + command

    def get_file(self, source_file_path, target_local_file_path):
        cmd = [
            'kubectl',
            'cp',
            self.namespace + "/" + self.pod_name + ":" + source_file_path,
            target_local_file_path]
        return subprocess.call(cmd, env=self.env_config)

    def put_file(self, source_file_path, target_file_path):
        cmd = [
            'kubectl',
            'cp',
            source_file_path,
            self.namespace + "/" + self.pod_name + ":" + target_file_path]
        return subprocess.call(cmd, env=self.env_config)

    def get_command_output(self, cmd, stdout=None):
        cmd = self.wrap_command(cmd)
        return subprocess.call(cmd, stdout=stdout, env=self.env_config)

    def exec_command(self, cmd):
        cmd = self.wrap_command(cmd)
        return subprocess.check_output(cmd, env=self.env_config).decode()

    def exec_script(self, local_script_name, params):
        '''
        Function to execute a local bash script on the k8s cluster.
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

        # Cannot use self.exec_command() because it needs '/bin/bash' and '-c' before the command
        wrapped_command = ['kubectl', 'exec', '-n', self.namespace, '-c',
                           'yb-master' if self.is_master else 'yb-tserver', self.pod_name, '--',
                           '/bin/bash', '-c', command]

        output = subprocess.check_output(wrapped_command, env=self.env_config).decode()
        return output


class SshParamikoClient:

    def __init__(self, args):
        self.key_filename = args.key
        self.ip = args.ip
        self.port = args.port
        self.client = None

    def connect(self):
        attempt = 1
        while True:
            try:
                self.client = paramiko.SSHClient()
                self.client.set_missing_host_key_policy(paramiko.MissingHostKeyPolicy())
                self.client.connect(self.ip, self.port, username=YB_USERNAME,
                                    key_filename=self.key_filename, timeout=10,
                                    banner_timeout=20, auth_timeout=20)
                return
            except socket.error as ex:
                logging.info("Failed to establish SSH connection to {}:{} - {}"
                             .format(self.ip, self.port, str(ex)))
                if attempt >= CONNECTION_RETRY_COUNT:
                    raise ex
                attempt += 1
                time.sleep(CONNECTION_RETRY_DELAY_SEC)

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
        if isinstance(cmd, str):
            command = cmd
        else:
            # Need to join with spaces, but surround arguments with spaces using "'" character
            command = ' '.join(
                list(map(lambda part: part if ' ' not in part else "'" + part + "'", cmd)))
        stdin, stdout, stderr = self.client.exec_command(command, timeout=COMMAND_TIMEOUT_SEC)
        return_code = stdout.channel.recv_exit_status()
        if return_code != 0:
            error = self.read_output(stderr)
            raise RuntimeError('Command \'{}\' returned error code {}: {}'
                               .format(command, return_code, error))
        output = self.read_output(stdout)
        return output

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
        stdin, stdout, stderr = self.client.exec_command(command, timeout=COMMAND_TIMEOUT_SEC)

        return_code = stdout.channel.recv_exit_status()
        if return_code != 0:
            error = self.read_output(stderr)
            raise RuntimeError('Command \'{}\' returned error code {}: {}'
                               .format(command, return_code, error))
        output = self.read_output(stdout)
        return output

    # We saw this script hang. The only place which can hang in theory is ssh command execution
    # and reading it's results.
    # Applied one of described workaround from this issue:
    # https://github.com/paramiko/paramiko/issues/109
    def read_output(self, stream):
        end_time = time.time() + COMMAND_TIMEOUT_SEC
        while not stream.channel.eof_received:
            time.sleep(1)
            if time.time() > end_time:
                stream.channel.close()
            break
        return stream.read().decode()
