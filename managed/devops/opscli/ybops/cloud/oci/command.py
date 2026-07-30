#!/usr/bin/env python
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from ybops.cloud.common.command import (
    InstanceCommand, QueryCommand, AccessCommand, NetworkCommand
)
from ybops.cloud.common.method import (
    ConfigureInstancesMethod, ListInstancesMethod,
    InitYSQLMethod, CronCheckMethod,
    AccessDeleteKeyMethod, TransferXClusterCerts,
    VerifySSHConnection, RebootInstancesMethod,
    RunHooks, WaitForConnection, ManageOtelCollector
)
from ybops.cloud.oci.method import (
    OciCreateInstancesMethod, OciProvisionInstancesMethod,
    OciDestroyInstancesMethod, OciQueryRegionsMethod, OciQueryZonesMethod,
    OciQueryInstanceTypesMethod, OciQueryCurrentHostMethod, OciQueryVpcMethod,
    OciQueryDeviceNames, OciAccessAddKeyMethod, OciNetworkBootstrapMethod,
    OciNetworkQueryMethod, OciNetworkCleanupMethod, OciCreateRootVolumesMethod,
    OciDeleteRootVolumesMethod, OciReplaceRootVolumeMethod, OciChangeInstanceTypeMethod,
    OciPauseInstancesMethod, OciResumeInstancesMethod, OciUpdateDiskMethod, OciTagsMethod,
    OciHardRebootInstancesMethod
)


class OciInstanceCommand(InstanceCommand):

    def __init__(self):
        super(OciInstanceCommand, self).__init__()

    def add_methods(self):
        self.add_method(OciProvisionInstancesMethod(self))
        self.add_method(OciCreateInstancesMethod(self))
        self.add_method(OciCreateRootVolumesMethod(self))
        self.add_method(OciDeleteRootVolumesMethod(self))
        self.add_method(OciReplaceRootVolumeMethod(self))
        self.add_method(OciDestroyInstancesMethod(self))
        self.add_method(ListInstancesMethod(self))
        self.add_method(ConfigureInstancesMethod(self))
        self.add_method(InitYSQLMethod(self))
        self.add_method(OciUpdateDiskMethod(self))
        self.add_method(CronCheckMethod(self))
        self.add_method(OciChangeInstanceTypeMethod(self))
        self.add_method(OciPauseInstancesMethod(self))
        self.add_method(OciResumeInstancesMethod(self))
        self.add_method(OciTagsMethod(self))
        self.add_method(TransferXClusterCerts(self))
        self.add_method(VerifySSHConnection(self))
        self.add_method(RebootInstancesMethod(self))
        self.add_method(RunHooks(self))
        self.add_method(WaitForConnection(self))
        self.add_method(OciHardRebootInstancesMethod(self))
        self.add_method(ManageOtelCollector(self))


class OciQueryCommand(QueryCommand):

    def __init__(self):
        super(OciQueryCommand, self).__init__()

    def add_methods(self):
        self.add_method(OciQueryRegionsMethod(self))
        self.add_method(OciQueryZonesMethod(self))
        self.add_method(OciQueryInstanceTypesMethod(self))
        self.add_method(OciQueryCurrentHostMethod(self))
        self.add_method(OciQueryVpcMethod(self))
        self.add_method(OciQueryDeviceNames(self))


class OciAccessCommand(AccessCommand):

    def __init__(self):
        super(OciAccessCommand, self).__init__()

    def add_methods(self):
        self.add_method(OciAccessAddKeyMethod(self))
        self.add_method(AccessDeleteKeyMethod(self))


class OciNetworkCommand(NetworkCommand):

    def __init__(self):
        super(OciNetworkCommand, self).__init__()

    def add_methods(self):
        self.add_method(OciNetworkBootstrapMethod(self))
        self.add_method(OciNetworkQueryMethod(self))
        self.add_method(OciNetworkCleanupMethod(self))
