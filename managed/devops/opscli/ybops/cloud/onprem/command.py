#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from ybops.cloud.common.command import InstanceCommand, AccessCommand
from ybops.cloud.common.method import ConfigureInstancesMethod, ListInstancesMethod, \
    InitYSQLMethod, CronCheckMethod, TransferXClusterCerts, \
    VerifySSHConnection, AddAuthorizedKey, RemoveAuthorizedKey, \
    AccessEditVaultMethod, AccessCreateVaultMethod, AccessDeleteKeyMethod, RebootInstancesMethod, \
    RunHooks, WaitForConnection, ManageOtelCollector
from ybops.cloud.onprem.method import OnPremCreateInstancesMethod, OnPremDestroyInstancesMethod, \
    OnPremProvisionInstancesMethod, OnPremValidateMethod, \
    OnPremFillInstanceProvisionTemplateMethod, OnPremListInstancesMethod, \
    OnPremPrecheckInstanceMethod, OnPremAccessAddKeyMethod, OnPremInstallNodeAgentMethod, \
    OnPremVerifyCertificatesMethod


class OnPremInstanceCommand(InstanceCommand):
    """Subclass for on premise specific instance command baseclass. Supplies overrides for method
    hooks.
    """
    def __init__(self):
        super(OnPremInstanceCommand, self).__init__()

    def add_methods(self):
        self.add_method(OnPremProvisionInstancesMethod(self))
        self.add_method(OnPremCreateInstancesMethod(self))
        self.add_method(ConfigureInstancesMethod(self))
        self.add_method(OnPremDestroyInstancesMethod(self))
        self.add_method(OnPremListInstancesMethod(self))
        self.add_method(OnPremValidateMethod(self))
        self.add_method(OnPremPrecheckInstanceMethod(self))
        self.add_method(OnPremFillInstanceProvisionTemplateMethod(self))
        self.add_method(InitYSQLMethod(self))
        self.add_method(CronCheckMethod(self))
        self.add_method(TransferXClusterCerts(self))
        self.add_method(VerifySSHConnection(self))
        self.add_method(AddAuthorizedKey(self))
        self.add_method(RemoveAuthorizedKey(self))
        self.add_method(RebootInstancesMethod(self))
        self.add_method(RunHooks(self))
        self.add_method(WaitForConnection(self))
        self.add_method(OnPremInstallNodeAgentMethod(self))
        self.add_method(ManageOtelCollector(self))
        self.add_method(OnPremVerifyCertificatesMethod(self))


class OnPremAccessCommand(AccessCommand):
    def __init__(self):
        super(OnPremAccessCommand, self).__init__()

    def add_methods(self):
        self.add_method(OnPremAccessAddKeyMethod(self))
        self.add_method(AccessCreateVaultMethod(self))
        self.add_method(AccessEditVaultMethod(self))
        self.add_method(AccessDeleteKeyMethod(self))
