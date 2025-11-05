#!/usr/bin/env python

from ybops.cloud.common.method import (
    AbstractAccessMethod, CreateInstancesMethod,
    ProvisionInstancesMethod, ListInstancesMethod,
    ConfigureInstancesMethod, InitYSQLMethod, CronCheckMethod,
    TransferXClusterCerts, RunHooks, WaitForConnection, ManageOtelCollector
)
import json
import logging
from six import iteritems


class KubernetesCreateInstancesMethod(CreateInstancesMethod):
    """Subclass for creating instances in Kubernetes.
    For Kubernetes, pods already exist, so this just validates connectivity.
    """
    def __init__(self, base_command):
        super(KubernetesCreateInstancesMethod, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesCreateInstancesMethod, self).update_ansible_vars_with_args(args)
        self.extra_vars['connection_type'] = 'kubectl'

        # Extract namespace from runtime_args if provided
        if hasattr(args, 'runtime_args') and args.runtime_args:
            try:
                runtime_args = json.loads(args.runtime_args)
                if 'YB_NAMESPACE' in runtime_args:
                    args.namespace = runtime_args['YB_NAMESPACE']
            except (json.JSONDecodeError, TypeError) as e:
                logging.warning(f"Failed to parse runtime_args: {str(e)}")

        host_info = self.cloud.get_host_info(args)
        if host_info:
            # Clear any pre-existing kubectl_container to avoid stale values
            self.extra_vars.pop('kubectl_container', None)

            self.extra_vars['kubectl_pod'] = host_info['name']
            self.extra_vars['kubectl_namespace'] = host_info.get('namespace', 'default')
            if host_info.get('container'):
                self.extra_vars['kubectl_container'] = host_info['container']
            if hasattr(args, 'kubeconfig') and args.kubeconfig:
                self.extra_vars['kubectl_kubeconfig'] = args.kubeconfig

    def callback(self, args):
        # Pods are already created in Kubernetes, just update vars and validate
        self.update_ansible_vars_with_args(args)
        self.wait_for_host(args)


class KubernetesProvisionInstancesMethod(ProvisionInstancesMethod):
    """Subclass for provisioning instances in Kubernetes.
    Skips pre-provisioning since pods are pre-existing.
    """
    def __init__(self, base_command):
        super(KubernetesProvisionInstancesMethod, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesProvisionInstancesMethod, self).update_ansible_vars_with_args(args)
        self.extra_vars['connection_type'] = 'kubectl'

        # Extract namespace from runtime_args if provided
        if hasattr(args, 'runtime_args') and args.runtime_args:
            try:
                runtime_args = json.loads(args.runtime_args)
                if 'YB_NAMESPACE' in runtime_args:
                    args.namespace = runtime_args['YB_NAMESPACE']
            except (json.JSONDecodeError, TypeError) as e:
                logging.warning(f"Failed to parse runtime_args: {str(e)}")

        host_info = self.cloud.get_host_info(args)
        if host_info:
            # Clear any pre-existing kubectl_container to avoid stale values
            self.extra_vars.pop('kubectl_container', None)

            self.extra_vars['kubectl_pod'] = host_info['name']
            self.extra_vars['kubectl_namespace'] = host_info.get('namespace', 'default')
            if host_info.get('container'):
                self.extra_vars['kubectl_container'] = host_info['container']
            if hasattr(args, 'kubeconfig') and args.kubeconfig:
                self.extra_vars['kubectl_kubeconfig'] = args.kubeconfig

    def callback(self, args):
        # For kubernetes, pods are pre-existing (managed by operator/helm)
        args.skip_preprovision = True
        super(KubernetesProvisionInstancesMethod, self).callback(args)


class KubernetesListInstancesMethod(ListInstancesMethod):
    """Subclass for listing instances in Kubernetes.
    """
    def __init__(self, base_command):
        super(KubernetesListInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(KubernetesListInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--namespace", default="default",
                                 help="Kubernetes namespace (default: default)")

    def callback(self, args):
        logging.debug(f"Listing Kubernetes instances with args: {args}")

        host_info = self.cloud.get_host_info(args, get_all=args.as_json)
        if not host_info:
            return None

        if args.as_json:
            print(json.dumps(host_info))
        else:
            # Single host info dict
            if isinstance(host_info, dict):
                print('\n'.join([f"{k}={v}" for k, v in iteritems(host_info)]))
            # List of host info dicts
            else:
                for info in host_info:
                    print('\n'.join([f"{k}={v}" for k, v in iteritems(info)]))


class KubernetesAccessAddKeyMethod(AbstractAccessMethod):
    """Subclass for adding SSH keys in Kubernetes.
    For Kubernetes with kubectl exec, SSH keys are less relevant but we provide basic support.
    """
    def __init__(self, base_command):
        super(KubernetesAccessAddKeyMethod, self).__init__(base_command, "add-key")

    def callback(self, args):
        logging.info("Adding SSH key for Kubernetes pod access")
        # For kubernetes, this is typically not needed as we use kubectl exec
        # But we implement it for compatibility
        print(json.dumps({"status": "success", "message": "Key management via kubectl"}))


def _setup_kubectl_connection(method_instance, args):
    """Helper function to set up kubectl connection parameters in extra_vars.
    This should be called by all Kubernetes methods that need remote execution.
    """
    # Extract namespace from runtime_args if provided
    if hasattr(args, 'runtime_args') and args.runtime_args:
        try:
            runtime_args = json.loads(args.runtime_args)
            if 'YB_NAMESPACE' in runtime_args:
                args.namespace = runtime_args['YB_NAMESPACE']
        except (json.JSONDecodeError, TypeError) as e:
            logging.warning(f"Failed to parse runtime_args: {str(e)}")

    host_info = method_instance.cloud.get_host_info(args)
    if host_info:
        # Clear any pre-existing kubectl_container to avoid stale values
        method_instance.extra_vars.pop('kubectl_container', None)

        method_instance.extra_vars['connection_type'] = 'kubectl'
        method_instance.extra_vars['kubectl_pod'] = host_info['name']
        method_instance.extra_vars['kubectl_namespace'] = host_info.get('namespace', 'default')
        if host_info.get('container'):
            method_instance.extra_vars['kubectl_container'] = host_info['container']
        if hasattr(args, 'kubeconfig') and args.kubeconfig:
            method_instance.extra_vars['kubectl_kubeconfig'] = args.kubeconfig


class KubernetesRunHooks(RunHooks):
    """Kubernetes-specific RunHooks that uses kubectl exec instead of SSH."""
    def __init__(self, base_command):
        super(KubernetesRunHooks, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesRunHooks, self).update_ansible_vars_with_args(args)
        _setup_kubectl_connection(self, args)


class KubernetesConfigureInstancesMethod(ConfigureInstancesMethod):
    """Kubernetes-specific ConfigureInstances that uses kubectl exec."""
    def __init__(self, base_command):
        super(KubernetesConfigureInstancesMethod, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesConfigureInstancesMethod, self).update_ansible_vars_with_args(args)
        _setup_kubectl_connection(self, args)


class KubernetesInitYSQLMethod(InitYSQLMethod):
    """Kubernetes-specific InitYSQL that uses kubectl exec."""
    def __init__(self, base_command):
        super(KubernetesInitYSQLMethod, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesInitYSQLMethod, self).update_ansible_vars_with_args(args)
        _setup_kubectl_connection(self, args)


class KubernetesCronCheckMethod(CronCheckMethod):
    """Kubernetes-specific CronCheck that uses kubectl exec."""
    def __init__(self, base_command):
        super(KubernetesCronCheckMethod, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesCronCheckMethod, self).update_ansible_vars_with_args(args)
        _setup_kubectl_connection(self, args)


class KubernetesTransferXClusterCerts(TransferXClusterCerts):
    """Kubernetes-specific TransferXClusterCerts that uses kubectl exec."""
    def __init__(self, base_command):
        super(KubernetesTransferXClusterCerts, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesTransferXClusterCerts, self).update_ansible_vars_with_args(args)
        _setup_kubectl_connection(self, args)


class KubernetesWaitForConnection(WaitForConnection):
    """Kubernetes-specific WaitForConnection that uses kubectl exec."""
    def __init__(self, base_command):
        super(KubernetesWaitForConnection, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesWaitForConnection, self).update_ansible_vars_with_args(args)
        _setup_kubectl_connection(self, args)


class KubernetesManageOtelCollector(ManageOtelCollector):
    """Kubernetes-specific ManageOtelCollector that uses kubectl exec."""
    def __init__(self, base_command):
        super(KubernetesManageOtelCollector, self).__init__(base_command)

    def update_ansible_vars_with_args(self, args):
        """Override to set connection_type to kubectl."""
        super(KubernetesManageOtelCollector, self).update_ansible_vars_with_args(args)
        _setup_kubectl_connection(self, args)
