#!/usr/bin/env python

from ybops.cloud.common.command import InstanceCommand, AccessCommand
from ybops.cloud.common.method import (
    AccessEditVaultMethod, AccessCreateVaultMethod, AccessDeleteKeyMethod
)
from ybops.cloud.kubernetes.method import (
    KubernetesCreateInstancesMethod, KubernetesProvisionInstancesMethod,
    KubernetesListInstancesMethod, KubernetesAccessAddKeyMethod,
    KubernetesConfigureInstancesMethod, KubernetesInitYSQLMethod,
    KubernetesCronCheckMethod, KubernetesTransferXClusterCerts,
    KubernetesRunHooks, KubernetesWaitForConnection,
    KubernetesManageOtelCollector
)


class KubernetesInstanceCommand(InstanceCommand):
    """Subclass for Kubernetes specific instance command.
    Most methods are reused from common, only override what's specific to kubernetes.
    """
    def __init__(self):
        super(KubernetesInstanceCommand, self).__init__()

    def add_methods(self):
        # All Kubernetes methods use kubectl exec instead of SSH
        self.add_method(KubernetesProvisionInstancesMethod(self))
        self.add_method(KubernetesCreateInstancesMethod(self))
        self.add_method(KubernetesListInstancesMethod(self))
        self.add_method(KubernetesConfigureInstancesMethod(self))
        self.add_method(KubernetesInitYSQLMethod(self))
        self.add_method(KubernetesCronCheckMethod(self))
        self.add_method(KubernetesTransferXClusterCerts(self))
        self.add_method(KubernetesRunHooks(self))
        self.add_method(KubernetesWaitForConnection(self))
        self.add_method(KubernetesManageOtelCollector(self))


class KubernetesAccessCommand(AccessCommand):
    """Subclass for Kubernetes specific access command.
    """
    def __init__(self):
        super(KubernetesAccessCommand, self).__init__()

    def add_methods(self):
        self.add_method(KubernetesAccessAddKeyMethod(self))
        self.add_method(AccessCreateVaultMethod(self))
        self.add_method(AccessEditVaultMethod(self))
        self.add_method(AccessDeleteKeyMethod(self))
