#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


from ybops.cloud.common.base import AbstractPerCloudCommand
from ybops.cloud.common.method import CreateInstancesMethod, ProvisionInstancesMethod, \
    DestroyInstancesMethod, ListInstancesMethod, ConfigureInstancesMethod, \
    ControlInstanceMethod, AbstractMethod, AccessCreateVaultMethod, InitYSQLMethod, \
    UpdateDiskMethod, CronCheckMethod, AccessEditVaultMethod, AccessDeleteKeyMethod, \
    CreateRootVolumesMethod, ReplaceRootVolumeMethod, ChangeInstanceTypeMethod, \
    UpdateMountedDisksMethod, DeleteRootVolumesMethod, TransferXClusterCerts, VerifySSHConnection, \
    AddAuthorizedKey, RemoveAuthorizedKey, RebootInstancesMethod, RunHooks, WaitForConnection, \
    ManageOtelCollector


class InstanceCommand(AbstractPerCloudCommand):
    """Class of commands related to instances.
    """
    def __init__(self):
        super(InstanceCommand, self).__init__("instance")

    def add_subcommands(self):
        self.add_subcommand(ControlInstanceCommand())

    def add_methods(self):
        """Override to prepare all the hooks for subcommands.
        """
        self.add_method(CreateInstancesMethod(self))
        self.add_method(CreateRootVolumesMethod(self))
        self.add_method(DeleteRootVolumesMethod(self))
        self.add_method(ReplaceRootVolumeMethod(self))
        self.add_method(ProvisionInstancesMethod(self))
        self.add_method(DestroyInstancesMethod(self))
        self.add_method(ListInstancesMethod(self))
        self.add_method(ConfigureInstancesMethod(self))
        self.add_method(InitYSQLMethod(self))
        self.add_method(UpdateDiskMethod(self))
        self.add_method(UpdateMountedDisksMethod(self))
        self.add_method(CronCheckMethod(self))
        self.add_method(ChangeInstanceTypeMethod(self))
        self.add_method(TransferXClusterCerts(self))
        self.add_method(VerifySSHConnection(self))
        self.add_method(AddAuthorizedKey(self))
        self.add_method(RemoveAuthorizedKey(self))
        self.add_method(RebootInstancesMethod(self))
        self.add_method(RunHooks(self))
        self.add_method(WaitForConnection(self))
        self.add_method(ManageOtelCollector(self))


class NetworkCommand(AbstractPerCloudCommand):
    """Class of commands related to network.
    """
    def __init__(self):
        super(NetworkCommand, self).__init__("network")

    def add_methods(self):
        """Override to prepare all the hooks for subcommands.
        """
        self.add_method(AbstractMethod(self, "bootstrap"))


class ControlInstanceCommand(AbstractPerCloudCommand):
    BASE_COMMANDS = ("start", "stop", "clean", "clean-logs")
    MASTER_COMMANDS = BASE_COMMANDS + ("create",)

    def __init__(self):
        super(ControlInstanceCommand, self).__init__("control")
        self.commands_per_server_type = {
            "master": self.MASTER_COMMANDS,
            "tserver": self.BASE_COMMANDS,
            "controller": self.BASE_COMMANDS
            }

    def add_subcommands(self):
        for node, commands in self.commands_per_server_type.items():
            self.add_subcommand(ServerControlInstanceCommand(node, commands))


class ServerControlInstanceCommand(AbstractPerCloudCommand):
    def __init__(self, node_type, control_commands=[]):
        super(ServerControlInstanceCommand, self).__init__(node_type)
        self.control_commands = control_commands

    def add_methods(self):
        for c in self.control_commands:
            self.add_method(ControlInstanceMethod(self, c))


class AccessCommand(AbstractPerCloudCommand):
    """Class of commands related to cloud access.
    """
    def __init__(self):
        super(AccessCommand, self).__init__("access")

    def add_methods(self):
        self.add_method(AccessCreateVaultMethod(self))
        self.add_method(AccessEditVaultMethod(self))
        self.add_method(AccessDeleteKeyMethod(self))


class QueryCommand(AbstractPerCloudCommand):
    """Class of commands related to querying cloud information.
    """
    def __init__(self):
        super(QueryCommand, self).__init__("query")


class DnsCommand(AbstractPerCloudCommand):
    def __init__(self):
        super(DnsCommand, self).__init__("dns")
