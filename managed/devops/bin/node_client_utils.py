import paramiko
import subprocess

YB_USERNAME = 'yugabyte'


class KubernetesClient:
    def __init__(self, args):
        self.namespace = args.namespace
        self.node_name = args.node_name
        self.is_master = args.is_master

    def wrap_command(self, cmd):
        return ['kubectl', 'exec', '-n', self.namespace, '-c',
                'yb-master' if self.is_master else 'yb-tserver', self.node_name, '--'] + cmd

    def get_command_output(self, cmd, stdout=None):
        cmd = self.wrap_command(cmd)
        return subprocess.call(cmd, stdout=stdout)

    def exec_command(self, cmd):
        cmd = self.wrap_command(cmd)
        return subprocess.check_output(cmd).decode()


class SshParamikoClient:
    def __init__(self, args):
        self.key_filename = args.key
        self.ip = args.ip
        self.port = args.port
        self.client = None

    def connect(self):
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.MissingHostKeyPolicy())
        self.client.connect(self.ip, self.port, username=YB_USERNAME,
                            key_filename=self.key_filename, timeout=10)

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
            command = ' '.join(cmd)
        stdin, stdout, stderr = self.client.exec_command(command)
        return_code = stdout.channel.recv_exit_status()
        if return_code != 0:
            error = stderr.read().decode()
            raise RuntimeError('Command returned error code {}: {}'.format(command, error))
        output = stdout.read().decode()
        return output
