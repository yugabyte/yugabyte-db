# Copyright 2020 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from ybops.cloud.common.command import InstanceCommand, NetworkCommand, AccessCommand, \
    QueryCommand, DnsCommand
from ybops.cloud.azure.method import AzureNetworkBootstrapMethod, AzureProvisionInstancesMethod, \
    AzureCreateInstancesMethod, AzureAccessAddKeyMethod, AzureQueryVPCMethod, \
    AzureQueryRegionsMethod, AzureQueryZonesMethod, AzureDestroyInstancesMethod, \
    AzureQueryInstanceTypesMethod, AzureQueryVnetMethod, AzureNetworkCleanupMethod, \
    AzureQueryUltraMethod, AzureCreateDnsEntryMethod, AzureEditDnsEntryMethod, \
    AzureDeleteDnsEntryMethod, AzureListDnsEntryMethod, AzureDeleteRootVolumesMethod, \
    AzurePauseInstancesMethod, AzureResumeInstancesMethod, AzureChangeInstanceTypeMethod, \
    AzureCreateRootVolumesMethod, AzureReplaceRootVolumeMethod, AzureHardRebootInstancesMethod
from ybops.cloud.common.method import AccessCreateVaultMethod, ConfigureInstancesMethod, \
    ListInstancesMethod, InitYSQLMethod, UpdateDiskMethod, CronCheckMethod, \
    AccessEditVaultMethod, AccessDeleteKeyMethod, TransferXClusterCerts, \
    VerifySSHConnection, AddAuthorizedKey, RemoveAuthorizedKey, RebootInstancesMethod, RunHooks, \
    WaitForConnection


class AzureNetworkCommand(NetworkCommand):
    def __init__(self):
        super(AzureNetworkCommand, self).__init__()

    def add_methods(self):
        self.add_method(AzureNetworkBootstrapMethod(self))
        self.add_method(AzureNetworkCleanupMethod(self))


class AzureInstanceCommand(InstanceCommand):
    def __init__(self):
        super(AzureInstanceCommand, self).__init__()

    def add_methods(self):
        self.add_method(AzureProvisionInstancesMethod(self))
        self.add_method(AzureCreateInstancesMethod(self))
        self.add_method(AzureDestroyInstancesMethod(self))
        self.add_method(ConfigureInstancesMethod(self))
        self.add_method(ListInstancesMethod(self))
        self.add_method(InitYSQLMethod(self))
        self.add_method(UpdateDiskMethod(self))
        self.add_method(CronCheckMethod(self))
        self.add_method(AzureDeleteRootVolumesMethod(self))
        self.add_method(AzureChangeInstanceTypeMethod(self))
        self.add_method(TransferXClusterCerts(self))
        self.add_method(VerifySSHConnection(self))
        self.add_method(AddAuthorizedKey(self))
        self.add_method(RemoveAuthorizedKey(self))
        self.add_method(AzurePauseInstancesMethod(self))
        self.add_method(AzureResumeInstancesMethod(self))
        self.add_method(RebootInstancesMethod(self))
        self.add_method(RunHooks(self))
        self.add_method(WaitForConnection(self))
        self.add_method(AzureCreateRootVolumesMethod(self))
        self.add_method(AzureReplaceRootVolumeMethod(self))
        self.add_method(AzureHardRebootInstancesMethod(self))


class AzureAccessCommand(AccessCommand):
    def __init__(self):
        super(AzureAccessCommand, self).__init__()

    def add_methods(self):
        self.add_method(AzureAccessAddKeyMethod(self))
        self.add_method(AccessCreateVaultMethod(self))
        self.add_method(AccessEditVaultMethod(self))
        self.add_method(AccessDeleteKeyMethod(self))


class AzureQueryCommand(QueryCommand):
    def __init__(self):
        super(AzureQueryCommand, self).__init__()

    def add_methods(self):
        self.add_method(AzureQueryVPCMethod(self))
        self.add_method(AzureQueryRegionsMethod(self))
        self.add_method(AzureQueryZonesMethod(self))
        self.add_method(AzureQueryInstanceTypesMethod(self))
        self.add_method(AzureQueryVnetMethod(self))
        self.add_method(AzureQueryUltraMethod(self))


class AzureDnsCommand(DnsCommand):
    def __init__(self):
        super(AzureDnsCommand, self).__init__()

    def add_methods(self):
        self.add_method(AzureCreateDnsEntryMethod(self))
        self.add_method(AzureEditDnsEntryMethod(self))
        self.add_method(AzureDeleteDnsEntryMethod(self))
        self.add_method(AzureListDnsEntryMethod(self))
