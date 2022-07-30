import os
import subprocess

YB_USERNAME = 'yugabyte'
# Let's set some timeout to our commands.
# If 10 minutes will not be enough for something - will have to pass command timeout as an argument.
# Just having timeout in shell script, which we're running on the node,
# does not seem to always help - as ssh client connection itself or command results read can hang.
COMMAND_TIMEOUT_SEC = 600


class KubernetesClient:
    def __init__(self, args):
        self.namespace = args.namespace
        # MultiAZ deployments have hostname_az in their name.
        self.node_name = args.node_name.split('_')[0]
        self.is_master = args.is_master
        self.env_config = os.environ.copy()
        self.env_config["KUBECONFIG"] = args.kubeconfig

    def wrap_command(self, cmd):
        if isinstance(cmd, str):
            cmd = cmd.split()
        return ['kubectl', 'exec', '-n', self.namespace, '-c',
                'yb-master' if self.is_master else 'yb-tserver', self.node_name, '--'] + cmd

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
