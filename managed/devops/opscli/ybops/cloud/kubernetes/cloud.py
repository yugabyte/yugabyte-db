#!/usr/bin/env python

import logging
import subprocess
from ybops.cloud.common.cloud import AbstractCloud
from ybops.cloud.common.method import AbstractInstancesMethod
from ybops.cloud.kubernetes.command import KubernetesInstanceCommand, KubernetesAccessCommand
from ybops.common.exceptions import YBOpsRuntimeError


class KubernetesCloud(AbstractCloud):
    """Subclass specific to Kubernetes cloud related functionality.
    Commands are executed in target pods using kubectl exec.
    """
    def __init__(self):
        super(KubernetesCloud, self).__init__("kubernetes")

    def add_extra_args(self):
        """Override to setup cloud-specific command line flags.
        """
        super(KubernetesCloud, self).add_extra_args()
        self.parser.add_argument("--namespace", required=False, default="default",
                                help="Kubernetes namespace")
        self.parser.add_argument("--kubeconfig", required=False,
                                help="Path to kubeconfig file")

    def add_subcommands(self):
        """Override to setup the cloud-specific instances of the subcommands.
        """
        self.add_subcommand(KubernetesInstanceCommand())
        self.add_subcommand(KubernetesAccessCommand())

    def _find_pod_by_labels(self, node_name, namespace, kubeconfig=None):
        """Find the actual pod name using Kubernetes label selectors.

        Args:
            node_name: Simplified node name (e.g., yb-master-0_eu-west-1a or yb-master-0)
            namespace: Kubernetes namespace
            kubeconfig: Path to kubeconfig file (optional)

        Returns:
            Tuple of (pod_name, container_name) where container_name is 'yb-master' or 'yb-tserver'
        """
        # Parse the node name to extract server type, index, and zone
        # Format: yb-master-0 or yb-master-0_eu-west-1a
        parts = node_name.split('_')
        base_name = parts[0]  # yb-master-0 or yb-tserver-0
        zone = parts[1] if len(parts) > 1 else None  # eu-west-1a (optional)

        # Determine server type and extract pod index
        # The server type is also the container name
        if base_name.startswith('yb-master-'):
            server_type = 'yb-master'
            pod_index = base_name[len('yb-master-'):]
        elif base_name.startswith('yb-tserver-'):
            server_type = 'yb-tserver'
            pod_index = base_name[len('yb-tserver-'):]
        else:
            raise YBOpsRuntimeError(
                f"Invalid node name format: {node_name}. Expected yb-master-N or yb-tserver-N")

        # Build kubectl command with label selectors
        kubectl_cmd = ["kubectl", "get", "pods"]

        if kubeconfig:
            kubectl_cmd.extend(["--kubeconfig", kubeconfig])

        kubectl_cmd.extend(["-n", namespace])

        # Build label selector (comma-separated for AND logic)
        labels = f"app.kubernetes.io/name={server_type},apps.kubernetes.io/pod-index={pod_index}"
        if zone:
            labels += f",yugabyte.io/zone={zone}"

        kubectl_cmd.extend(["-l", labels])

        kubectl_cmd.extend(["-o", "jsonpath={.items[0].metadata.name}"])

        logging.debug(f"Finding pod with command: {' '.join(kubectl_cmd)}")

        try:
            result = subprocess.run(
                kubectl_cmd,
                capture_output=True,
                text=True,
                check=True
            )

            if not result.stdout.strip():
                zone_info = f", zone={zone}" if zone else ""
                raise YBOpsRuntimeError(
                    f"No pod found for node '{node_name}' with labels "
                    f"component={server_type}, pod-index={pod_index}{zone_info}")

            pod_name = result.stdout.strip()
            return pod_name, server_type

        except subprocess.CalledProcessError as e:
            raise YBOpsRuntimeError(f"Failed to find pod for node '{node_name}': {e.stderr}")

    def get_host_info(self, args, get_all=False):
        """Override to get host info for Kubernetes pods.

        For kubernetes, search_pattern is the simplified node name (e.g., yb-tserver-0_eu-west-1b).
        We use label selectors to find the actual pod name.
        """
        node_name = args.search_pattern if hasattr(args, 'search_pattern') else None

        if not node_name:
            return None

        namespace = args.namespace if hasattr(args, 'namespace') else "default"
        kubeconfig = args.kubeconfig if hasattr(args, 'kubeconfig') else None

        # Find the actual pod name and container using label selectors
        actual_pod_name, container_name = self._find_pod_by_labels(node_name, namespace, kubeconfig)

        result = dict(
            name=actual_pod_name,  # Use actual pod name for kubectl exec
            # For kubernetes, we use kubectl exec, not direct IP
            public_ip=actual_pod_name,
            private_ip=actual_pod_name,
            region=args.region if hasattr(args, 'region') else "local",
            zone=args.zone if hasattr(args, 'zone') else "local",
            namespace=namespace,
            container=container_name,  # Include container name derived from node name
            instance_type="kubernetes-pod",
            server_type=AbstractInstancesMethod.YB_SERVER_TYPE
        )

        if get_all:
            return [result]
        return result
