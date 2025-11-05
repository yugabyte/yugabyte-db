#!/usr/bin/env python

import logging
import subprocess

from ybops.common.exceptions import YBOpsRecoverableError, YBOpsRuntimeError


class KubectlClient(object):
    """KubectlClient class is used to run commands in Kubernetes pods using kubectl exec.
    """

    def __init__(self, pod_name, namespace="default", kubeconfig=None, container=None):
        """
        Initialize kubectl client.

        Args:
            pod_name: Name of the pod to execute commands in
            namespace: Kubernetes namespace (default: "default")
            kubeconfig: Path to kubeconfig file (optional)
            container: Container name within the pod (optional)
        """
        self.pod_name = pod_name
        self.namespace = namespace
        self.kubeconfig = kubeconfig
        self.container = container

    def _build_kubectl_cmd(self, command):
        """Build the base kubectl exec command."""
        kubectl_cmd = ["kubectl"]

        if self.kubeconfig:
            kubectl_cmd.extend(["--kubeconfig", self.kubeconfig])

        kubectl_cmd.extend([
            "exec",
            "-n", self.namespace,
            self.pod_name
        ])

        if self.container:
            kubectl_cmd.extend(["-c", self.container])

        kubectl_cmd.append("--")

        # Add the actual command
        if isinstance(command, list):
            kubectl_cmd.extend(command)
        else:
            kubectl_cmd.extend(["/bin/bash", "-c", command])

        return kubectl_cmd

    def exec_command(self, command, output_only=False, **kwargs):
        """
        Execute a command in the pod using kubectl exec.

        Args:
            command: Command to execute (string or list)
            output_only: If True, return only stdout. If False, return (rc, stdout, stderr)
            **kwargs: Additional options (ignored for compatibility)

        Returns:
            If output_only=True: stdout string
            If output_only=False: tuple of (return_code, stdout, stderr)
        """
        kubectl_cmd = self._build_kubectl_cmd(command)
        logging.debug(f"Executing kubectl command: {' '.join(kubectl_cmd)}")

        try:
            result = subprocess.run(
                kubectl_cmd,
                capture_output=True,
                text=True,
                timeout=kwargs.get('timeout', None)
            )

            if output_only:
                if result.returncode != 0:
                    raise YBOpsRecoverableError(
                        f"Command '{command}' failed with return code {result.returncode} and error: {result.stderr}")
                return result.stdout
            else:
                # Log the output for YBA to capture
                if result.stdout:
                    logging.info(result.stdout)
                if result.stderr:
                    logging.warning(result.stderr)

                # Return stdout/stderr as lists of lines (compatible with SSH implementation)
                stdout_lines = result.stdout.splitlines(keepends=True) if result.stdout else []
                stderr_lines = result.stderr.splitlines(keepends=True) if result.stderr else []
                return result.returncode, stdout_lines, stderr_lines

        except subprocess.TimeoutExpired:
            raise YBOpsRecoverableError(f"Command timed out: {command}")
        except Exception as e:
            raise YBOpsRecoverableError(f"Failed to execute command: {str(e)}")

    def exec_script(self, local_script_name, params):
        """
        Execute a local script on the remote pod.

        Args:
            local_script_name: Path to local script file
            params: Parameters to pass to the script (string or list)

        Returns:
            stdout of the script execution
        """
        if not isinstance(params, str):
            params = ' '.join(params)

        with open(local_script_name, "r") as f:
            local_script = f.read()

        # Use heredoc syntax to pass script content
        command = f"/bin/bash -s {params} <<'EOF'\n{local_script}\nEOF"

        return self.exec_command(command, output_only=True)

    def upload_file_to_remote_server(self, local_path, remote_path, **kwargs):
        """
        Upload a file to the pod using kubectl cp.

        Args:
            local_path: Path to local file
            remote_path: Destination path in the pod
            **kwargs: Additional options (chmod, etc.)
        """
        kubectl_cmd = ["kubectl"]

        if self.kubeconfig:
            kubectl_cmd.extend(["--kubeconfig", self.kubeconfig])

        kubectl_cmd.extend(["cp", "-n", self.namespace])

        if self.container:
            kubectl_cmd.extend(["-c", self.container])

        kubectl_cmd.extend([local_path, f"{self.pod_name}:{remote_path}"])

        logging.debug(f"Copying file with command: {' '.join(kubectl_cmd)}")

        try:
            result = subprocess.run(
                kubectl_cmd,
                capture_output=True,
                text=True,
                check=True
            )

            if result.stderr:
                logging.warning(f"kubectl cp stderr: {result.stderr}")

            logging.info(f"kubectl cp completed with rc={result.returncode}")

            # Apply chmod if specified
            if kwargs.get('chmod'):
                # Mask out file type bits, keep permission bits, and ensure execution bit is set
                permissions = (kwargs.get('chmod') & 0o7777) | 0o0111
                chmod_cmd = f"chmod {permissions:o} {remote_path}"
                logging.debug(f"Applying chmod: {chmod_cmd}")
                self.exec_command(chmod_cmd, output_only=True)

        except subprocess.CalledProcessError as e:
            logging.error(f"kubectl cp failed: stdout={e.stdout}, stderr={e.stderr}, rc={e.returncode}")
            raise YBOpsRuntimeError(f"Failed to copy file {local_path} to pod: {e.stderr}")
        except Exception as e:
            logging.error(f"Unexpected error during kubectl cp: {str(e)}")
            raise

    def download_file_from_remote_server(self, remote_file_name, local_file_name, **kwargs):
        """
        Download a file from the pod using kubectl cp.

        Args:
            remote_file_name: Path to file in the pod
            local_file_name: Destination path on local filesystem
            **kwargs: Additional options
        """
        kubectl_cmd = ["kubectl"]

        if self.kubeconfig:
            kubectl_cmd.extend(["--kubeconfig", self.kubeconfig])

        kubectl_cmd.extend(["cp", "-n", self.namespace])

        if self.container:
            kubectl_cmd.extend(["-c", self.container])

        kubectl_cmd.extend([f"{self.pod_name}:{remote_file_name}", local_file_name])

        logging.debug(f"Downloading file with command: {' '.join(kubectl_cmd)}")

        try:
            subprocess.run(
                kubectl_cmd,
                capture_output=True,
                text=True,
                check=True
            )

            logging.info(f"Successfully downloaded {self.pod_name}:{remote_file_name} to {local_file_name}")

        except subprocess.CalledProcessError as e:
            raise YBOpsRuntimeError(f"Failed to download file {remote_file_name} from pod: {e.stderr}")

    def close_connection(self):
        """Close connection (no-op for kubectl, but kept for compatibility)."""
        pass
