#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import getpass
import glob
import json
import logging
import os
import random
import re
import string
import sys
import time
import datetime

from pprint import pprint
from ybops.cloud.common.cloud import InstanceState
from ybops.common.exceptions import YBOpsRuntimeError, YBOpsRecoverableError
from ybops.utils import get_path_from_yb, \
    generate_random_password, validate_cron_status, \
    YB_SUDO_PASS, DEFAULT_MASTER_HTTP_PORT, DEFAULT_MASTER_RPC_PORT, DEFAULT_TSERVER_HTTP_PORT, \
    DEFAULT_TSERVER_RPC_PORT, DEFAULT_CQL_PROXY_RPC_PORT, DEFAULT_REDIS_PROXY_RPC_PORT
from ansible_vault import Vault
from ybops.utils.remote_shell import copy_to_tmp, wait_for_server, get_host_port_user, RemoteShell
from ybops.utils.ssh import wait_for_ssh, format_rsa_key, validated_key_file, \
    generate_rsa_keypair, get_public_key_content, \
    get_ssh_host_port, DEFAULT_SSH_USER, DEFAULT_SSH_PORT
from ybops.utils import remote_exec_command


class ConsoleLoggingErrorHandler(object):
    def __init__(self, cloud):
        self.cloud = cloud

    def __call__(self, exception, args):
        if args.search_pattern:
            console_output = self.cloud.get_console_output(args)

            if console_output:
                timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
                out_file_path = (f"{args.remote_tmp_dir}/{args.search_pattern}-{timestamp}"
                                 + "-console.log")
                logging.warning(f"Dumping latest console output to {out_file_path}")

                with open(out_file_path, 'a') as f:
                    f.write(console_output + '\n')


class AbstractMethod(object):
    """Class for lower level abstractions of final commands, that contain the final method to be
    executed, when the parsing is done.
    """

    def __init__(self, base_command, name):
        self.base_command = base_command
        self.cloud = base_command.cloud
        self.name = name
        self.parser = base_command.parser
        # Set this to False if the respective method does not need credential validation.
        self.need_validation = True
        self.error_handler = None

    def prepare(self):
        """Hook for setting up parser options.
        """
        logging.debug("...preparing {}".format(self.name))

        self.parser = self.base_command.subparsers.add_parser(self.name)
        callback_wrapper = getattr(self, "callback_wrapper", None)
        self.parser.set_defaults(func=callback_wrapper)

        self.add_extra_args()

    def add_extra_args(self):
        """Hook for subclasses to add extra parsing arguments that may be shared with other methods
        that are not part of the same enheritance chain.
        """
        self.parser.add_argument("--vault_password_file", default=None)
        self.parser.add_argument("--ask_sudo_pass", action='store_true', default=False)
        self.parser.add_argument("--vars_file", default=None)
        self.parser.add_argument("--ssh2_enabled", action='store_true', default=False)
        self.parser.add_argument("--connection_type", default=None, required=False)
        self.parser.add_argument("--architecture", required=False, help="Architecture for machine "
                                 + "image. Defaults to x86_64.", default="x86_64")
        self.parser.add_argument("--remote_tmp_dir", default="/tmp",
                                 help="Temp directory on the remote host.")
        self.parser.add_argument("--tmp_dir", default="/tmp",
                                 help="Temp directory on the local host.")

    def preprocess_args(self, args):
        """Hook for pre-processing args before actually executing the callback. Useful for shared
        default processing on similar methods with a shared abstract superclass.
        """
        logging.debug("...preprocessing {}".format(self.name))

    def callback_wrapper(self, args):
        """Hook for setting up actual command execution.
        """
        logging.debug("...calling {}".format(self.name))
        self.cloud.init(args)
        if self.need_validation:
            self.cloud.validate_credentials()
        self.cloud.init_cloud_api(args)
        self.preprocess_args(args)
        try:
            self.callback(args)
        except BaseException as e:
            logging.exception(e)
            if self.error_handler:
                self.error_handler(e, args)
            raise e

    def _cleanup_dir(self, path):
        for file in glob.glob("{}/*.*".format(path)):
            os.remove(file)
        os.rmdir(path)


class AbstractInstancesMethod(AbstractMethod):
    """Superclass for instance-specific method preparation, such as the Ansible extra_vars and the
    options for search_pattern, as all instance methods can take instances as parameters.
    """
    YB_SERVER_TYPE = "cluster-server"
    SSH_USER = "centos"
    INSTANCE_LOOKUP_RETRY_LIMIT = 2

    def __init__(self, base_command, name, required_host=True):
        super(AbstractInstancesMethod, self).__init__(base_command, name)
        self.extra_vars = dict()
        self.required_host = required_host

    def add_extra_args(self):
        """Add the generic instance specific options.
        """
        super(AbstractInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--cloud_subnet",
                                 required=False,
                                 help="The VPC subnet id into which we want to provision")
        self.parser.add_argument("--cloud_subnet_secondary",
                                 required=False,
                                 help="The VPC subnet id into which we want to provision "
                                 "the secondary network interface")
        if self.required_host:
            self.parser.add_argument("search_pattern", default=None)
        else:
            self.parser.add_argument("search_pattern", nargs="?")
        self.parser.add_argument("-t", "--type", default=self.YB_SERVER_TYPE)
        self.parser.add_argument('--tags', action='append', default=None)
        self.parser.add_argument("--skip_tags", action='append', default=None)

        # If we do not have this entry from ansible.env, then set a None default, else, assume the
        # pem file is in the same location as the ansible.env file.
        default_key_pair = os.environ.get("YB_EC2_KEY_PAIR_NAME")
        if default_key_pair is not None:
            default_key_pair = get_path_from_yb(default_key_pair + ".pem")

        self.parser.add_argument("--private_key_file", default=default_key_pair)
        self.parser.add_argument("--volume_size", type=int, default=250,
                                 help="desired size (gb) of each volume mounted on instance")
        self.parser.add_argument("--instance_type",
                                 required=False,
                                 help="The instance type to act on")
        self.parser.add_argument("--cloud_instance_types", nargs='*',
                                 required=False,
                                 default=[],
                                 help="Priority list of vendor instance types.")
        self.parser.add_argument("--ssh_user",
                                 required=False,
                                 help="The username for ssh")
        self.parser.add_argument("--custom_ssh_port",
                                 required=False,
                                 help="The ssh port to connect to.")
        self.parser.add_argument("--instance_tags",
                                 required=False,
                                 help="Tags for instances being created.")
        self.parser.add_argument("--systemd_services",
                                 action="store_true",
                                 default=False,
                                 help="check if systemd services is set")
        self.parser.add_argument("--configure_ybc",
                                 action="store_true",
                                 default=False,
                                 help="configure yb-controller on node.")
        self.parser.add_argument("--machine_image",
                                 required=False,
                                 help="The machine image (e.g. an AMI on AWS) to install, "
                                      "this depends on the region.")
        self.parser.add_argument("--boot_script", required=False,
                                 help="Custom boot script to execute on the instance.")
        self.parser.add_argument("--boot_script_token", required=False,
                                 help="Custom boot script token in /etc/yb-boot-script-complete")
        self.parser.add_argument("--node_agent_ip", required=False,
                                 help="Node agent server ip")
        self.parser.add_argument("--node_agent_port", required=False,
                                 help="Node agent server port")
        self.parser.add_argument("--node_agent_cert_path", required=False,
                                 help="Node agent cert path")
        self.parser.add_argument("--node_agent_auth_token", required=False,
                                 help="Node agent auth token")
        self.parser.add_argument("--node_agent_home", required=False,
                                 help="Node agent home path")
        self.parser.add_argument("--offload_ansible",
                                 required=False,
                                 action="store_true",
                                 help="Offload ansible tasks to the DB node")
        self.parser.add_argument("--imdsv2required",
                                 action="store_true",
                                 default=False,
                                 help="IMDSv2 Required.")

        mutex_group = self.parser.add_mutually_exclusive_group()
        mutex_group.add_argument("--num_volumes", type=int, default=0,
                                 help="number of volumes to mount at the default path (/mnt/d#)")
        mutex_group.add_argument("--mount_points", default=None,
                                 help="comma-separated path(s) to drive(s) mounted on instance")

    def get_ssh_user(self):
        """Used by subclasses to ensure an appropriate ssh_user is used for certain operations"""
        return None

    def update_ansible_vars_with_args(self, args):
        """Hook for subclasses to update Ansible extra-vars with arguments passed."""
        updated_args = {
            "server_type": args.type,
            "instance_name": args.search_pattern,
            "tags": args.tags,
            "skip_tags": args.skip_tags,
            "private_key_file": args.private_key_file,
            "ssh2_enabled": args.ssh2_enabled,
            "remote_tmp_dir": args.remote_tmp_dir
        }
        if args.vars_file:
            updated_args["vars_file"] = args.vars_file
        if args.vault_password_file:
            updated_args["vault_password_file"] = args.vault_password_file
        if args.ask_sudo_pass:
            updated_args["ask_sudo_pass"] = True
        if args.volume_size:
            updated_args["ssd_size_gb"] = args.volume_size

        if args.mount_points:
            self.mount_points = args.mount_points.strip()
            updated_args["mount_points"] = self.mount_points
            updated_args["num_volumes"] = len(self.mount_points)
        elif args.num_volumes:
            updated_args["mount_points"] = self.cloud.get_mount_points_csv(args)
            updated_args["num_volumes"] = args.num_volumes

        if args.instance_type:
            updated_args["instance_type"] = args.instance_type

        # Handle all ssh defaults in update. Then use self.extra_vars
        if args.ssh_user:
            updated_args["ssh_user"] = args.ssh_user
        elif self.get_ssh_user():
            updated_args["ssh_user"] = self.get_ssh_user()
        else:
            updated_args["ssh_user"] = self.SSH_USER

        if args.connection_type:
            updated_args["connection_type"] = args.connection_type
            # TODO have separate user for node-agent.
            updated_args["node_agent_user"] = updated_args["ssh_user"]
        if args.node_agent_ip:
            updated_args["node_agent_ip"] = args.node_agent_ip
        if args.node_agent_port:
            updated_args["node_agent_port"] = args.node_agent_port
        if args.node_agent_cert_path:
            updated_args["node_agent_cert_path"] = args.node_agent_cert_path
        if args.node_agent_auth_token:
            updated_args["node_agent_auth_token"] = args.node_agent_auth_token
        if args.node_agent_home:
            updated_args["node_agent_home"] = args.node_agent_home

        if args.instance_tags:
            updated_args["instance_tags"] = json.loads(args.instance_tags)
        updated_args["offload_ansible"] = args.offload_ansible

        self.extra_vars.update(updated_args)
        self.extra_vars["imdsv2required"] = args.imdsv2required

    def update_ansible_vars_with_host_info(self, host_info, custom_ssh_port):
        """Hook for subclasses to update Ansible extra-vars with host specifics before calling out.
        """
        self.extra_vars.update({
            "private_ip": host_info["private_ip"],
            "public_ip": host_info["public_ip"],
            "placement_cloud": self.cloud.name,
            "placement_region": host_info["region"],
            "placement_zone": host_info["zone"],
            "instance_name": host_info["name"],
            "instance_type": host_info["instance_type"]
        })
        self.extra_vars.update(self.get_server_host_port(host_info, custom_ssh_port))

    def wait_for_host(self, args, default_port=True):
        logging.info("Waiting for instance {}".format(args.search_pattern))
        host_lookup_count = 0
        # Cache the result of the cloud call outside of the loop.
        host_info = None

        while host_lookup_count < self.INSTANCE_LOOKUP_RETRY_LIMIT:
            if not host_info or not host_info.get("is_running"):
                host_info = self.cloud.get_host_info(args)

            if host_info:
                self.extra_vars.update(self.get_server_host_port(
                    host_info, args.custom_ssh_port, default_port))
                self.update_open_ssh_port(args)
                if wait_for_server(self.extra_vars):
                    return host_info

            sys.stdout.write('.')
            sys.stdout.flush()
            time.sleep(1)
            host_lookup_count += 1

        host_port_user = get_host_port_user(self.extra_vars)
        raise YBOpsRecoverableError("Timed out waiting for instance: '{}'. {}@{}:{} using {}"
                                    .format(args.search_pattern, host_port_user["user"],
                                            host_port_user["host"], host_port_user["port"],
                                            host_port_user["connection_type"]))

    def get_connection_type(self, args=None):
        connection_type = self.extra_vars.get("connection_type")
        if connection_type is None and args and hasattr(args, "connection_type"):
            connection_type = args.connection_type
        if connection_type is None:
            return 'ssh'
        return connection_type

    # Find the open ssh port and update the dictionary.
    def update_open_ssh_port(self, args):
        ssh_port_updated = False
        connection_type = self.get_connection_type(args)
        if connection_type == 'ssh':
            ssh_ports = [self.extra_vars["ssh_port"]]
            if args.custom_ssh_port and int(args.custom_ssh_port) != self.extra_vars["ssh_port"]:
                ssh_ports.append(int(args.custom_ssh_port))
            ssh_port = self.cloud.wait_for_server_ports(
                self.extra_vars["ssh_host"], args.search_pattern, ssh_ports)
            if self.extra_vars["ssh_port"] != ssh_port:
                self.extra_vars["ssh_port"] = ssh_port
                ssh_port_updated = True
        return ssh_port_updated

    def get_server_host_port(self, host_info, custom_ssh_port, default_port=False):
        connection_type = self.get_connection_type()
        if connection_type == 'ssh':
            return get_ssh_host_port(host_info, custom_ssh_port, default_port)
        if connection_type == 'node_agent_rpc':
            return {
                "node_agent_ip": host_info["private_ip"],
                "node_agent_port": self.extra_vars["node_agent_port"]
            }
        raise YBOpsRuntimeError("Unknown connction type: {}".format(connection_type))

    def get_server_ports_to_check(self, args):
        server_ports = []
        connection_type = self.get_connection_type(args)
        if connection_type is None or connection_type == 'ssh':
            server_ports.append(DEFAULT_SSH_PORT)
            ssh_port = self.extra_vars.get("ssh_port")
            if ssh_port:
                server_ports.append(ssh_port)
            if args.custom_ssh_port:
                server_ports.append(int(args.custom_ssh_port))
        elif connection_type == 'node_agent_rpc':
            server_ports.append(self.extra_vars["node_agent_port"])
        return list(set(server_ports))


class VerifySSHConnection(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(VerifySSHConnection, self).__init__(base_command, "verify_node_ssh_access", True)

    def add_extra_args(self):
        super(VerifySSHConnection, self).add_extra_args()
        self.parser.add_argument("--new_private_key_file",
                                 required=True,
                                 help="Private key file path of newly added \
                                 key for verifying connection")

    def callback(self, args):
        if args.new_private_key_file == args.private_key_file:
            print("Given old and new access keys " +
                  "are the same, skipping.")
            return
        host_info = self.cloud.get_host_info(args)
        ssh_details = get_ssh_host_port(host_info, args.custom_ssh_port, default_port=False)
        ssh_retries = 3
        oldKeyConnects = wait_for_ssh(ssh_details["ssh_host"],
                                      args.custom_ssh_port,
                                      args.ssh_user,
                                      args.private_key_file,
                                      ssh_retries, ssh2_enabled=args.ssh2_enabled)
        if oldKeyConnects:
            print("SSH connection verification successful")
            return
        elif args.new_private_key_file is None:
            raise YBOpsRuntimeError("SSH connection verification failed! \
                Could not connect with given SSH key for instance: '{0}' \
                    and no alternate new key provided".format(
                args.search_pattern))
        else:
            # make SSH key rotation idempotent - it will move ahead if new key connects
            newKeyConnects = wait_for_ssh(ssh_details["ssh_host"],
                                          args.custom_ssh_port,
                                          args.ssh_user,
                                          args.new_private_key_file,
                                          ssh_retries, ssh2_enabled=args.ssh2_enabled)
            if newKeyConnects:
                print("New key already connects " +
                      "whereas old key does not")
                return
            else:
                raise YBOpsRecoverableError("SSH connection verification failed! " +
                                            "Could not connect with old or new " +
                                            "SSH key for instance: '{0}'".format(
                                                args.search_pattern))


class AddAuthorizedKey(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(AddAuthorizedKey, self).__init__(base_command, "add_authorized_key", True)

    def add_extra_args(self):
        super(AddAuthorizedKey, self).add_extra_args()
        self.parser.add_argument("--public_key_content",
                                 required=True,
                                 help="Public key content to be added to authorized_keys of node")
        self.parser.add_argument("--new_private_key_file",
                                 required=True,
                                 help="Private key file path of newly added \
                                 key for verifying connection")

    def callback(self, args):
        if args.private_key_file == args.new_private_key_file:
            print("Given old and new access keys" +
                  " are the same, skipping.")
            return
        host_info = self.cloud.get_host_info(args)
        ssh_details = get_ssh_host_port(host_info, args.custom_ssh_port, default_port=False)
        ssh_retries = 3
        # check connection before adding public key to make task idempotent
        newKeyConnects = wait_for_ssh(ssh_details["ssh_host"],
                                      args.custom_ssh_port,
                                      args.ssh_user,
                                      args.new_private_key_file,
                                      ssh_retries, ssh2_enabled=args.ssh2_enabled)
        if newKeyConnects:
            print("Given SSH key already exists " +
                  "for user {} in instance: {}".format(
                      args.ssh_user, args.search_pattern))
            return

        # add public new key
        public_key_content = args.public_key_content
        if args.public_key_content == "":
            # public key is taken by parsing private key file in cases when
            # a customer uploads only private key file
            public_key_content = get_public_key_content(args.new_private_key_file)
        updated_vars = {
            "command": "add-authorized-key",
            "public_key_content": public_key_content
        }
        self.update_ansible_vars_with_args(args)
        self.update_ansible_vars_with_host_info(host_info, args.custom_ssh_port)
        updated_vars.update(self.extra_vars)
        self.cloud.setup_ansible(args).run("edit_authorized_keys.yml", updated_vars)

        # confirm connection with new key
        newKeyConnects = wait_for_ssh(ssh_details["ssh_host"],
                                      args.custom_ssh_port,
                                      args.ssh_user,
                                      args.new_private_key_file,
                                      ssh_retries, ssh2_enabled=args.ssh2_enabled)
        if newKeyConnects:
            print("Add access key successful")
        else:
            raise YBOpsRecoverableError("Add authorized key failed! " +
                                        "Could not connect with given " +
                                        "SSH key for instance: '{0}'".format(
                                            args.search_pattern))


class RemoveAuthorizedKey(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(RemoveAuthorizedKey, self).__init__(base_command, "remove_authorized_key", True)

    def add_extra_args(self):
        super(RemoveAuthorizedKey, self).add_extra_args()
        self.parser.add_argument("--public_key_content",
                                 required=True,
                                 help="Public key content to be removed from \
                                     authorized_keys of node")
        self.parser.add_argument("--old_private_key_file",
                                 required=True,
                                 help="Private key file path of key to be removed \
                                 for verifying connection")

    def callback(self, args):
        if args.private_key_file == args.old_private_key_file:
            print("Given old and new access keys " +
                  "are the same, skipping.")
            return
        host_info = self.cloud.get_host_info(args)
        ssh_details = get_ssh_host_port(host_info, args.custom_ssh_port, default_port=False)
        ssh_retries = 3
        # check connection before removing public key to make task idempotent
        oldKeyConnects = wait_for_ssh(ssh_details["ssh_host"],
                                      args.custom_ssh_port,
                                      args.ssh_user,
                                      args.old_private_key_file,
                                      ssh_retries, ssh2_enabled=args.ssh2_enabled)

        if not oldKeyConnects:
            print("SSH key already removed/does not " +
                  "exist for instance: {}".format(
                      args.search_pattern))
            return

        # remove public key
        public_key_content = args.public_key_content
        if args.public_key_content == "":
            # public key is taken by parsing private key file in cases when
            # a customer uploads only private key file
            public_key_content = get_public_key_content(args.old_private_key_file)
        updated_vars = {
            "command": "remove-authorized-key",
            "public_key_content": public_key_content
        }
        self.update_ansible_vars_with_args(args)
        self.update_ansible_vars_with_host_info(host_info, args.custom_ssh_port)
        updated_vars.update(self.extra_vars)
        self.cloud.setup_ansible(args).run("edit_authorized_keys.yml", updated_vars)

        # confirm connection with old key does not work
        oldKeyConnects = wait_for_ssh(ssh_details["ssh_host"],
                                      args.custom_ssh_port,
                                      args.ssh_user,
                                      args.old_private_key_file,
                                      ssh_retries, ssh2_enabled=args.ssh2_enabled)
        if not oldKeyConnects:
            print("Remove access key successful")
        else:
            raise YBOpsRuntimeError("Could not remove SSH key for instance: '{0}'".format(
                args.search_pattern))


class ReplaceRootVolumeMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(ReplaceRootVolumeMethod, self).__init__(base_command, "replace_root_volume")

    def add_extra_args(self):
        super(ReplaceRootVolumeMethod, self).add_extra_args()
        self.parser.add_argument("--replacement_disk",
                                 required=True,
                                 help="The new boot disk to attach to the instance")

    def _replace_root_volume(self, args, host_info, current_root_volume):
        unmounted = False
        try:
            id = args.search_pattern
            logging.info("==> Stopping instance {}".format(id))
            self.cloud.stop_instance(host_info)
            # Azure does not require unmounting because replacement is done in one API call.
            if current_root_volume and self.cloud.name != "azu":
                self.cloud.unmount_disk(host_info, current_root_volume)
                unmounted = True
                logging.info("==> Root volume {} unmounted from {}".format(
                    current_root_volume, id))
            logging.info("==> Mounting {} as the new root volume on {}".format(
                args.replacement_disk, id))
            self._mount_root_volume(host_info, args.replacement_disk)
        except Exception as e:
            logging.exception(e)
            if unmounted:
                # Note: This doesn't work if VM image upgrade has already been done successfully
                # at least once before. The boot disk name will no longer correlate directly with
                # the instance name(id).
                old_disk_url = os.path.join(
                    args.replacement_disk[:args.replacement_disk.rfind('/')], id)
                self._mount_root_volume(host_info, old_disk_url)
                logging.warning("Mounted the original volume {} on {} before failing".format(
                                old_disk_url, id))
            raise e
        finally:
            if args.boot_script is not None:
                self.cloud.update_user_data(args)
            server_ports = self.get_server_ports_to_check(args)
            self.cloud.start_instance(host_info, server_ports)

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)

        if not host_info:
            raise YBOpsRuntimeError(
                "Instance {} not found, was it stopped?".format(args.search_pattern))
        self.update_ansible_vars_with_args(args)
        self._replace_root_volume(args, *self._host_info_with_current_root_volume(args, host_info))


class DestroyInstancesMethod(AbstractInstancesMethod):
    """Superclass for destroying an instance.
    """

    def __init__(self, base_command):
        super(DestroyInstancesMethod, self).__init__(base_command, "destroy")

    def add_extra_args(self):
        super(DestroyInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--node_ip", default=None,
                                 help="The ip of the instance to delete.")
        self.parser.add_argument(
            "--delete_static_public_ip",
            action="store_true",
            default=False,
            help="Delete the static public ip.")
        self.parser.add_argument("--node_uuid", default=None,
                                 help="The uuid of the instance to delete.")

    def callback(self, args):
        self.update_ansible_vars_with_args(args)
        self.cloud.setup_ansible(args).run("destroy-instance.yml", self.extra_vars)


class CreateInstancesMethod(AbstractInstancesMethod):
    """Superclass for creating an instance.

    This class will create an instance, if one does not already exist with the same conditions,
    such as name, region or zone, etc.
    """

    def __init__(self, base_command):
        super(CreateInstancesMethod, self).__init__(base_command, "create")
        self.can_ssh = True
        self.error_handler = ConsoleLoggingErrorHandler(self.cloud)

    def add_extra_args(self):
        """Setup the CLI options for creating instances.
        """
        super(CreateInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--assign_public_ip",
                                 action="store_true",
                                 default=False,
                                 help="The ip of the instance to provision.")

        self.parser.add_argument("--assign_static_public_ip",
                                 action="store_true",
                                 default=False,
                                 help="Assign a static public ip to the instance.")

        self.parser.add_argument("--use_spot_instance",
                                 action="store_true",
                                 default=False,
                                 help="Use Spot instance.")

        self.parser.add_argument("--boot_disk_size_gb",
                                 type=int,
                                 default=40,
                                 help="Size of the boot disk in GBs. Currently only works on GCP.")

        self.parser.add_argument("--auto_delete_boot_disk",
                                 action="store_true",
                                 default=True,
                                 help="Delete the root volume on VM termination.")
        self.parser.add_argument("-j", "--as_json", action="store_true")

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if host_info:
            raise YBOpsRuntimeError("Host {} already created.".format(args.search_pattern))

        self.extra_vars.update({
            "volume_type": args.volume_type
        })
        self.update_ansible_vars_with_args(args)
        create_output = self.run_ansible_create(args)
        host_info = self.cloud.get_host_info(args)
        normalized_state = self.cloud.normalize_instance_state(host_info.get("instance_state"))
        logging.info("Host {} is in normalized state {}({})."
                     .format(args.search_pattern, normalized_state,
                             host_info.get("instance_state")))
        # Onprem nodes report unknown state.
        if normalized_state not in (InstanceState.UNKNOWN, InstanceState.STARTING,
                                    InstanceState.RUNNING):
            raise YBOpsRecoverableError("Host {} is in invalid state {}."
                                        .format(args.search_pattern,
                                                host_info.get("instance_state")))
        # Set the host and default port.
        self.extra_vars.update(
            self.get_server_host_port(host_info, args.custom_ssh_port, default_port=True))
        # Update with the open port.
        self.update_open_ssh_port(args)
        self.extra_vars['ssh_user'] = self.extra_vars.get("ssh_user", DEFAULT_SSH_USER)
        # Port is already open. Wait for connection to server to succeed.
        connected = wait_for_server(self.extra_vars)
        if not connected:
            host_port_user = get_host_port_user(self.extra_vars)
            raise YBOpsRecoverableError("Connection({}) to host {} by user {} failed at port {}"
                                        .format(host_port_user["connection_type"],
                                                host_port_user["host"],
                                                host_port_user["user"],
                                                host_port_user["port"]))

        if args.boot_script:
            logging.info(
                'Waiting for the startup script to finish on {}'.format(args.search_pattern))
            retries = 0
            while not self.cloud.wait_for_startup_script(args, self.extra_vars) and retries < 5:
                retries += 1
                time.sleep(2 ** retries)

            # For clusters with secondary subnets, the start-up script is expected to fail.
            if not args.cloud_subnet_secondary:
                self.cloud.verify_startup_script(args, self.extra_vars)

            logging.info('Startup script finished on {}'.format(args.search_pattern))
        if create_output is not None:
            host_info.update(create_output)
        print(json.dumps(host_info))


class ProvisionInstancesMethod(AbstractInstancesMethod):
    """Superclass for provisioning an instance.

    This will create an instance, if needed, hence a reference to a Create method.
    """
    DEFAULT_OS_NAME = "centos"

    def __init__(self, base_command):
        self.create_method = None
        super(ProvisionInstancesMethod, self).__init__(base_command, "provision")
        self.error_handler = ConsoleLoggingErrorHandler(self.cloud)

    def preprocess_args(self, args):
        super(ProvisionInstancesMethod, self).preprocess_args(args)

    def add_extra_args(self):
        """Override to be able to prepare the same arguments as a Create, as well as all the extra
        arguments specific to this class.
        """
        super(ProvisionInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--air_gap", action="store_true", help="Run airgapped install.")
        self.parser.add_argument("--skip_preprovision", action="store_true", default=False)
        self.parser.add_argument("--local_package_path",
                                 required=False,
                                 help="Path to local directory with third party software tarballs.")
        self.parser.add_argument("--node_exporter_port", type=int, default=9300,
                                 help="The port for node_exporter to bind to.")
        self.parser.add_argument("--node_exporter_user", default="prometheus")
        self.parser.add_argument("--install_node_exporter", action="store_true")
        self.parser.add_argument('--remote_package_path', default=None,
                                 help="Path to download thirdparty packages "
                                      "for itest. Only for AWS/onprem.")
        self.parser.add_argument("--os_name",
                                 required=False,
                                 help="The os name to provision the universe in.",
                                 default=self.DEFAULT_OS_NAME,
                                 type=str.lower)
        self.parser.add_argument("--disable_custom_ssh", action="store_true",
                                 help="Disable running the ansible task for using custom SSH.")
        self.parser.add_argument("--pg_max_mem_mb", type=int, default=0,
                                 help="Max memory for postgress process.")
        self.parser.add_argument("--use_chrony", action="store_true",
                                 help="Whether to set up chrony for NTP synchronization.")
        self.parser.add_argument("--ntp_server", required=False, action="append",
                                 help="NTP server to connect to.")
        self.parser.add_argument("--lun_indexes", default="",
                                 help="Comma-separated LUN indexes for mounted on instance disks.")
        self.parser.add_argument("--install_locales", action="store_true", default=False,
                                 help="If enabled YBA will install locale on the DB nodes")
        self.parser.add_argument("--install_otel_collector", action="store_true")
        self.parser.add_argument('--otel_col_config_file', default=None,
                                 help="Path to OpenTelemetry Collector config file.")
        self.parser.add_argument('--otel_col_aws_access_key', default=None,
                                 help="AWS Access Key used for logs export")
        self.parser.add_argument('--otel_col_aws_secret_key', default=None,
                                 help="AWS Secret Key used for logs export.")
        self.parser.add_argument('--otel_col_gcp_creds_file', default=None,
                                 help="Path to GCP credentials file used for logs export.")
        self.parser.add_argument('--ycql_audit_log_level', default=None,
                                 help="YCQL audit log level.")

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Could not find host {} to provision!".format(
                args.search_pattern))

        self.update_ansible_vars_with_args(args)

        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port,
                                                         default_port=True))

        # Check if ssh port has already been updated.
        # This updates extra_vars with the open port.
        ssh_port_updated = self.update_open_ssh_port(args)
        use_default_ssh_port = not ssh_port_updated

        # Check if secondary subnet is present. If so, configure it.
        if host_info.get('secondary_subnet'):
            # Wait for host to be ready to run ssh commands.
            self.wait_for_host(args, use_default_ssh_port)
            server_ports = self.get_server_ports_to_check(args)
            self.cloud.configure_secondary_interface(
                args, self.extra_vars, self.cloud.get_subnet_cidr(args,
                                                                  host_info['secondary_subnet']),
                server_ports)
            # The bootscript might have failed due to no access to public internet
            # Re-run it.
            if args.boot_script:
                # copy and run the script
                self.cloud.execute_boot_script(args, self.extra_vars)

        if not args.skip_preprovision:
            self.preprovision(args)

        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port))
        if args.local_package_path:
            self.extra_vars.update({"local_package_path": args.local_package_path})
        if args.air_gap:
            self.extra_vars.update({"air_gap": args.air_gap})
        if args.node_exporter_port:
            self.extra_vars.update({"node_exporter_port": args.node_exporter_port})
        if args.install_node_exporter:
            self.extra_vars.update({"install_node_exporter": args.install_node_exporter})
        if args.node_exporter_user:
            self.extra_vars.update({"node_exporter_user": args.node_exporter_user})
        if args.remote_package_path:
            self.extra_vars.update({"remote_package_path": args.remote_package_path})
        if args.pg_max_mem_mb:
            self.extra_vars.update({"pg_max_mem_mb": args.pg_max_mem_mb})
        if args.ntp_server:
            self.extra_vars.update({"ntp_servers": args.ntp_server})
        self.extra_vars["use_chrony"] = args.use_chrony
        self.extra_vars.update({"systemd_option": args.systemd_services})
        self.extra_vars.update({"instance_type": args.instance_type})
        self.extra_vars.update({"configure_ybc": args.configure_ybc})
        self.extra_vars["device_names"] = self.get_device_names(args, host_info)
        self.extra_vars["lun_indexes"] = args.lun_indexes
        if args.install_otel_collector:
            self.extra_vars.update({"install_otel_collector": args.install_otel_collector})
        if args.otel_col_config_file:
            self.extra_vars.update({"otel_col_config_file_local": args.otel_col_config_file})
        if args.otel_col_aws_access_key:
            self.extra_vars.update({"otel_col_aws_access_key": args.otel_col_aws_access_key})
        if args.otel_col_aws_secret_key:
            self.extra_vars.update({"otel_col_aws_secret_key": args.otel_col_aws_secret_key})
        if args.otel_col_gcp_creds_file:
            self.extra_vars.update({"otel_col_gcp_creds_local": args.otel_col_gcp_creds_file})
        if args.ycql_audit_log_level:
            self.extra_vars.update({"ycql_audit_log_level": args.ycql_audit_log_level})

        if wait_for_server(self.extra_vars):
            self.cloud.setup_ansible(args).run("yb-server-provision.yml",
                                               self.extra_vars, host_info)
        else:
            host_port_user = get_host_port_user(self.extra_vars)
            raise YBOpsRecoverableError("Could not connect({}) into node {}:{} using username {}"
                                        .format(host_port_user["connection_type"],
                                                host_port_user["host"],
                                                host_port_user["user"],
                                                host_port_user["port"]))

    def get_device_names(self, args, host_info=None):
        return self.cloud.get_device_names(args)

    def update_ansible_vars(self, args):
        for arg_name in ["cloud_subnet",
                         "machine_image",
                         "instance_type",
                         "num_volumes",
                         "os_name"]:
            arg_value = getattr(args, arg_name)
            if arg_value is not None:
                self.extra_vars[arg_name] = arg_value

        # The reason we can't put network_name in the loop above is that the Ansible variable name
        # ("network_name") is different from the ybcloud argument name ("network") in this case.
        if args.network is not None:
            self.extra_vars["network_name"] = args.network

    def preprovision(self, args):
        self.update_ansible_vars(args)
        ssh_port_updated = self.update_open_ssh_port(args)
        use_default_ssh_port = not ssh_port_updated
        host_info = self.wait_for_host(args, use_default_ssh_port)
        ansible = self.cloud.setup_ansible(args)
        if args.install_locales:
            self.extra_vars["install_locales"] = True
        ansible.run("preprovision.yml", self.extra_vars, host_info, disable_offloading=True)

        # Disabling custom_ssh_port for onprem provider when ssh2_enabled, because
        # we won't be able to know whether the nodes used are having openssh/tectia server.
        if not args.disable_custom_ssh and use_default_ssh_port and \
                not (args.ssh2_enabled and self.cloud.name == "onprem") and \
                int(args.custom_ssh_port) != DEFAULT_SSH_PORT:
            ansible.run("use_custom_ssh_port.yml", self.extra_vars, host_info)


class CreateRootVolumesMethod(AbstractInstancesMethod):
    """Superclass for create root volumes.
    """

    def __init__(self, base_command):
        super(CreateRootVolumesMethod, self).__init__(base_command, "create_root_volumes")
        self.create_method = CreateInstancesMethod(self.base_command)

    def add_extra_args(self):
        self.create_method.parser = self.parser
        self.create_method.add_extra_args()

        self.parser.add_argument("--num_disks",
                                 required=False,
                                 default=1,
                                 help="The number of boot disks to allocate in the zone.")

    def preprocess_args(self, args):
        super(CreateRootVolumesMethod, self).preprocess_args(args)
        self.create_method.preprocess_args(args)

    def callback(self, args):
        unique_string = ''.join(random.choice(string.ascii_lowercase) for i in range(6))
        args.search_pattern = "{}-".format(unique_string) + args.search_pattern
        volume_id = self.create_master_volume(args)
        root_volumes = [volume_id]
        logging.info("Create master volume output {}".format(root_volumes))
        num_disks = int(args.num_disks) - 1
        snapshot_creation_delay = None
        snapshot_creation_max_attempts = None

        try:
            snapshot_creation_delay = args.snapshot_creation_delay
        except Exception as e:
            pass  # Continue as none (default values if not found)

        try:
            snapshot_creation_max_attempts = args.snapshot_creation_max_attempts
        except Exception as e:
            pass  # Continue as none (default values if not found)

        # Now clone and create the remaining disks if any as the machine image is the same
        if num_disks > 0:
            logging.info("Cloning {} other disks using volume_id {}".format(num_disks, volume_id))
            if snapshot_creation_delay is not None and snapshot_creation_max_attempts is not None:
                root_volumes.extend(self.cloud.clone_disk(
                    args, volume_id, num_disks,
                    args.snapshot_creation_delay, args.snapshot_creation_max_attempts))
            else:
                root_volumes.extend(self.cloud.clone_disk(args, volume_id, num_disks))

        logging.info("==> Created volumes {}".format(root_volumes))
        output = {"boot_disks_per_zone": root_volumes}
        if self.cloud.name == "aws":
            root_device_name = self.cloud.get_image_root_label(args)
            if root_device_name:
                output["root_device_name"] = root_device_name
            else:
                raise YBOpsRuntimeError("Could not determine root device name for {}.".
                                        format(args.machine_image))
        print(json.dumps(output))


class DeleteRootVolumesMethod(AbstractInstancesMethod):
    """Superclass for deleting root volumes.
    """

    def __init__(self, base_command):
        super(DeleteRootVolumesMethod, self).__init__(base_command, "delete_root_volumes")

    def add_extra_args(self):
        super(DeleteRootVolumesMethod, self).add_extra_args()
        self.parser.add_argument("--volume_id",
                                 action="append",
                                 required=False,
                                 default=None,
                                 help="The ID of the volume to be deleted.")

    def delete_volumes(self, args):
        """Deletes volumes in detached state that match the tags
        or both tags and specific ids (names) if they are specified.
        """
        pass

    def callback(self, args):
        self.delete_volumes(args)


class ListInstancesMethod(AbstractInstancesMethod):
    """Superclass for listing all instances, potentially matching the given pattern.

    This class can print out data as JSON or simple K=V lines format from the overall metadata
    obtained from the cloud APIs.
    """

    def __init__(self, base_command):
        super(ListInstancesMethod, self).__init__(base_command, "list", required_host=False)

    def prepare(self):
        super(ListInstancesMethod, self).prepare()
        self.parser.add_argument("-j", "--as_json", action="store_true")

    def callback(self, args):
        # If we don't ask for JSON data, let's just return 1 item, as it's likely a scripted usage.
        host_info = self.cloud.get_host_info(args, get_all=args.as_json)
        if not host_info:
            return None

        if 'server_type' in host_info and host_info['server_type'] is None:
            del host_info['server_type']

        if args.as_json:
            print(json.dumps(host_info))
        else:
            print('\n'.join(["{}={}".format(k, v) for k, v in host_info.items()]))


class UpdateDiskMethod(AbstractInstancesMethod):
    """Superclass for updating the size of the disks associated with instances in
    the given pattern.

    """

    def __init__(self, base_command):
        super(UpdateDiskMethod, self).__init__(base_command, "disk_update")

    def add_extra_args(self):
        super(UpdateDiskMethod, self).add_extra_args()
        self.parser.add_argument("--force", action="store_true",
                                 default=False, help="Force disk update.")

    def prepare(self):
        super(UpdateDiskMethod, self).prepare()

    def callback(self, args):
        self.cloud.update_disk(args)
        host_info = self.cloud.get_host_info(args)
        self.update_ansible_vars_with_args(args)
        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port))
        self.cloud.expand_file_system(args, self.extra_vars)


class UpdateMountedDisksMethod(AbstractInstancesMethod):
    """Superclass for updating fstab for disks, see PLAT-2547.
    """

    def __init__(self, base_command):
        super(UpdateMountedDisksMethod, self).__init__(base_command, "update_mounted_disks")

    def update_ansible_vars_with_args(self, args):
        super(UpdateMountedDisksMethod, self).update_ansible_vars_with_args(args)

    def get_device_names(args, host_info=None):
        self.cloud.get_device_names(args)

    def callback(self, args):
        # Need to verify that all disks are mounted by UUUID
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Could not find host {} to provision!".format(
                args.search_pattern))
        ansible = self.cloud.setup_ansible(args)
        self.update_ansible_vars_with_args(args)
        self.extra_vars["device_names"] = self.get_device_names(args, host_info)
        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port))
        ansible.playbook_args["remote_role"] = "mount_ephemeral_drives"
        logging.debug(pprint(self.extra_vars))
        ansible.run("remote_role.yml", self.extra_vars, host_info)


class ChangeInstanceTypeMethod(AbstractInstancesMethod):
    """Superclass for resizing the instances (instance type) in the given pattern.
    """

    def __init__(self, base_command):
        super(ChangeInstanceTypeMethod, self).__init__(base_command, "change_instance_type")

    def add_extra_args(self):
        super(ChangeInstanceTypeMethod, self).add_extra_args()
        self.parser.add_argument("--pg_max_mem_mb", type=int, default=0,
                                 help="Max memory for postgress process.")
        self.parser.add_argument("--air_gap", action="store_true",
                                 default=False, help="Run airgapped install.")
        self.parser.add_argument("--force", action="store_true",
                                 default=False, help="Force instance type change.")

    def prepare(self):
        super(ChangeInstanceTypeMethod, self).prepare()

    def callback(self, args):
        self._validate_args(args)
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance {} not found".format(args.search_pattern))

        self.update_ansible_vars_with_host_info(host_info, args.custom_ssh_port)
        self.update_ansible_vars_with_args(args)
        self._resize_instance(args, self._host_info(args, host_info))

    def update_ansible_vars_with_args(self, args):
        super(ChangeInstanceTypeMethod, self).update_ansible_vars_with_args(args)
        self.extra_vars["pg_max_mem_mb"] = args.pg_max_mem_mb
        self.extra_vars["air_gap"] = args.air_gap
        self.extra_vars["systemd_option"] = args.systemd_services

    def _validate_args(self, args):
        # Make sure "instance_type" exists in args
        if args.instance_type is None:
            raise YBOpsRuntimeError("instance_type not defined. Please define your intended type"
                                    " using --instance_type argument.")

    def _resize_instance(self, args, host_info):
        try:
            if args.instance_type != host_info['instance_type'] or args.force:
                logging.info("Stopping instance {}".format(args.search_pattern))
                self.cloud.stop_instance(host_info)

                logging.info('Instance {} is stopped'.format(args.search_pattern))

                # Change instance type
                self._change_instance_type(args, host_info)
                logging.info(
                    "Instance %s\'s type changed to %s",
                    args.search_pattern, args.instance_type)
            else:
                logging.info(
                    "Instance %s\'s type has not changed from %s. Skipping.",
                    args.search_pattern, args.instance_type)
        except Exception as e:
            raise YBOpsRuntimeError('error executing \"instance.modify_attribute\": {}'
                                    .format(repr(e)))
        finally:
            if args.boot_script is not None:
                self.cloud.update_user_data(args)
            server_ports = self.get_server_ports_to_check(args)
            self.cloud.start_instance(host_info, server_ports)
            logging.info('Instance {} is started'.format(args.search_pattern))
        # Make sure we are using the updated cgroup value if instance type is changing.
        self.cloud.setup_ansible(args).run("setup-cgroup.yml", self.extra_vars, host_info)


class CronCheckMethod(AbstractInstancesMethod):
    """Superclass for checking cronjob status on specified node.
    """

    def __init__(self, base_command):
        super(CronCheckMethod, self).__init__(base_command, "croncheck")

    def get_ssh_user(self):
        # Force croncheck to be done on yugabyte user.
        return "yugabyte"

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        self.update_ansible_vars_with_args(args)
        connect_options = {}
        connect_options.update(self.extra_vars)
        connect_options.update({
            "ssh_user": self.get_ssh_user(),
            "node_agent_user": self.get_ssh_user()
        })
        connect_options.update(self.get_server_host_port(host_info, args.custom_ssh_port))
        if not args.systemd_services and not validate_cron_status(connect_options):
            host_port_user = get_host_port_user(connect_options)
            raise YBOpsRuntimeError(
                'Failed to find cronjobs on host {}'.format(host_port_user['host']))


class ConfigureInstancesMethod(AbstractInstancesMethod):
    VALID_PROCESS_TYPES = ['master', 'tserver']
    CERT_ROTATE_ACTIONS = ['APPEND_NEW_ROOT_CERT', 'ROTATE_CERTS',
                           'REMOVE_OLD_ROOT_CERT', 'UPDATE_CERT_DIRS']
    SKIP_CERT_VALIDATION_OPTIONS = ['ALL', 'HOSTNAME']
    # Child files and directories for master.
    MASTER_STATE_DIRS = ['wals', 'data', 'consensus-meta', 'instance',
                         'auto_flags_config', 'tablet-meta']

    def __init__(self, base_command):
        super(ConfigureInstancesMethod, self).__init__(base_command, "configure")
        self.supported_types = [self.YB_SERVER_TYPE]
        self.error_handler = ConsoleLoggingErrorHandler(self.cloud)

    def prepare(self):
        super(ConfigureInstancesMethod, self).prepare()

        self.parser.add_argument('--package', default=None)
        self.parser.add_argument('--num_releases_to_keep', type=int,
                                 help="Number of releases to keep after upgrade.")
        self.parser.add_argument('--ybc_package', default=None)
        self.parser.add_argument('--ybc_dir', default=None)
        self.parser.add_argument('--yb_process_type', default=None,
                                 choices=self.VALID_PROCESS_TYPES)
        self.parser.add_argument('--extra_gflags', default=None)
        self.parser.add_argument('--gflags', default=None)
        self.parser.add_argument('--gflags_to_remove', default=None)
        self.parser.add_argument('--ybc_flags', default=None)
        self.parser.add_argument('--master_addresses_for_tserver')
        self.parser.add_argument('--master_addresses_for_master')
        self.parser.add_argument('--server_broadcast_addresses')
        self.parser.add_argument('--root_cert_path')
        self.parser.add_argument('--server_cert_path')
        self.parser.add_argument('--server_key_path')
        self.parser.add_argument('--certs_location')
        self.parser.add_argument('--certs_node_dir')
        self.parser.add_argument('--root_cert_path_client_to_server')
        self.parser.add_argument('--server_cert_path_client_to_server')
        self.parser.add_argument('--server_key_path_client_to_server')
        self.parser.add_argument('--certs_location_client_to_server')
        self.parser.add_argument('--certs_client_dir')
        self.parser.add_argument('--client_cert_path')
        self.parser.add_argument('--client_key_path')
        self.parser.add_argument('--cert_rotate_action', default=None,
                                 choices=self.CERT_ROTATE_ACTIONS)
        self.parser.add_argument('--num_cores_to_keep', type=int, default=5,
                                 help="number of clean cores to keep in the ansible layer")
        self.parser.add_argument('--skip_cert_validation',
                                 default=None, choices=self.SKIP_CERT_VALIDATION_OPTIONS)
        self.parser.add_argument('--cert_valid_duration', default=365)
        self.parser.add_argument('--org_name', default="example.com")
        self.parser.add_argument('--encryption_key_source_file')
        self.parser.add_argument('--encryption_key_target_dir',
                                 default="yugabyte-encryption-files")

        self.parser.add_argument('--master_http_port', default=DEFAULT_MASTER_HTTP_PORT)
        self.parser.add_argument('--master_rpc_port', default=DEFAULT_MASTER_RPC_PORT)
        self.parser.add_argument('--tserver_http_port', default=DEFAULT_TSERVER_HTTP_PORT)
        self.parser.add_argument('--tserver_rpc_port', default=DEFAULT_TSERVER_RPC_PORT)
        self.parser.add_argument('--cql_proxy_rpc_port', default=DEFAULT_CQL_PROXY_RPC_PORT)
        self.parser.add_argument('--redis_proxy_rpc_port', default=DEFAULT_REDIS_PROXY_RPC_PORT)

        # Parameters for downloading YB package directly on DB nodes.
        self.parser.add_argument('--s3_remote_download', action="store_true")
        self.parser.add_argument('--aws_access_key')
        self.parser.add_argument('--aws_secret_key')
        self.parser.add_argument('--gcs_remote_download', action="store_true")
        self.parser.add_argument('--gcs_credentials_json')
        self.parser.add_argument('--http_remote_download', action="store_true")
        self.parser.add_argument('--http_package_checksum', default='')
        self.parser.add_argument('--install_third_party_packages',
                                 action="store_true",
                                 default=False)
        self.parser.add_argument("--local_package_path",
                                 required=False,
                                 help="Path to local directory with the third-party tarball.")
        # Development flag for itests.
        self.parser.add_argument('--itest_s3_package_path',
                                 help="Path to download packages for itest. Only for AWS/onprem.")
        self.parser.add_argument('--reset_master_state',
                                 help="Reset master state by deleting old files and directories.",
                                 action="store_true",
                                 default=False)
        self.parser.add_argument("--local_gflag_files_path",
                                 required=False,
                                 help="Path to local directory with the gFlags file.")
        self.parser.add_argument("--remote_gflag_files_path",
                                 required=False,
                                 help="Path to remote directory with the gFlags file.")
        self.parser.add_argument("--acceptable_clock_skew_wait_enabled",
                                 action="store_true",
                                 help="Whether ensure the clock skew is below the threshold.")
        self.parser.add_argument("--acceptable_clock_skew_sec",
                                 required=False,
                                 help="Maximum acceptable clock skew in seconds before starting "
                                      "the yb processes.")
        self.parser.add_argument("--acceptable_clock_skew_max_tries",
                                 required=False,
                                 help="The maximum number of checking the clock skew before "
                                      "failing.")

    def get_ssh_user(self):
        # Force the yugabyte user for configuring instances. The configure step performs YB specific
        # operations like creating directories under /home/yugabyte/ which can be performed only
        # by the yugabyte user without sudo. Since we don't want the configure step to require
        # sudo, we force the yugabyte user here.
        return "yugabyte"

    def callback(self, args):
        if args.type == self.YB_SERVER_TYPE:
            if args.master_addresses_for_tserver is None:
                raise YBOpsRuntimeError("Missing argument for YugaByte configure")
            self.extra_vars.update({
                "instance_name": args.search_pattern,
                "master_addresses_for_tserver": args.master_addresses_for_tserver,
                "master_http_port": args.master_http_port,
                "master_rpc_port": args.master_rpc_port,
                "tserver_http_port": args.tserver_http_port,
                "tserver_rpc_port": args.tserver_rpc_port,
                "cql_proxy_rpc_port": args.cql_proxy_rpc_port,
                "redis_proxy_rpc_port": args.redis_proxy_rpc_port,
                "cert_valid_duration": args.cert_valid_duration,
                "org_name": args.org_name,
                "certs_client_dir": args.certs_client_dir,
                "certs_node_dir": args.certs_node_dir,
                "encryption_key_dir": args.encryption_key_target_dir
            })

            if args.master_addresses_for_master is not None:
                self.extra_vars["master_addresses_for_master"] = args.master_addresses_for_master

            if args.server_broadcast_addresses is not None:
                self.extra_vars["server_broadcast_addresses"] = args.server_broadcast_addresses

            if args.yb_process_type:
                self.extra_vars["yb_process_type"] = args.yb_process_type.lower()
        else:
            raise YBOpsRuntimeError("Supported types for this command are only: {}".format(
                self.supported_types))

        self.extra_vars["systemd_option"] = args.systemd_services
        self.extra_vars["configure_ybc"] = args.configure_ybc
        self.extra_vars["yb_num_cores_to_keep"] = args.num_cores_to_keep

        # Make sure we set server_type so we pick the right configure.
        self.update_ansible_vars_with_args(args)

        if args.gflags is not None:
            # Allow gflag to be set during software upgrade 'install-software' to include
            #   cluster_uuid gflag for newer db releases.
            if args.package and "install-software" not in args.tags:
                raise YBOpsRuntimeError("When changing gflags, do not set packages info " +
                                        "unless installing software during software upgrade.")
            self.extra_vars["gflags"] = json.loads(args.gflags)

        if args.package is not None:
            self.extra_vars["package"] = args.package

        if args.num_releases_to_keep is not None:
            self.extra_vars["num_releases_to_keep"] = args.num_releases_to_keep
        if args.ybc_package is not None:
            self.extra_vars["ybc_package"] = args.ybc_package

        if args.ybc_dir is not None:
            self.extra_vars["ybc_dir"] = args.ybc_dir

        if args.ybc_flags is not None:
            self.extra_vars["ybc_flags"] = json.loads(args.ybc_flags)

        if args.extra_gflags is not None:
            self.extra_vars["extra_gflags"] = json.loads(args.extra_gflags)

        if args.gflags_to_remove is not None:
            self.extra_vars["gflags_to_remove"] = json.loads(args.gflags_to_remove)

        if args.root_cert_path is not None:
            self.extra_vars["root_cert_path"] = args.root_cert_path.strip()

        if args.acceptable_clock_skew_wait_enabled is not None:
            self.extra_vars["acceptable_clock_skew_wait_enabled"] = (
                args.acceptable_clock_skew_wait_enabled)

        if args.acceptable_clock_skew_sec is not None:
            self.extra_vars["acceptable_clock_skew_sec"] = args.acceptable_clock_skew_sec

        if args.acceptable_clock_skew_max_tries is not None:
            self.extra_vars["acceptable_clock_skew_max_tries"] = (
                args.acceptable_clock_skew_max_tries)

        if args.cert_rotate_action is not None:
            if args.cert_rotate_action not in self.CERT_ROTATE_ACTIONS:
                raise YBOpsRuntimeError(
                    "Supported actions for this command are only: {}".format(
                        self.CERT_ROTATE_ACTIONS))

        host_info = None
        if args.search_pattern != 'localhost':
            host_info = self.cloud.get_host_info(args)
            if not host_info:
                raise YBOpsRuntimeError("Instance: {} does not exist, cannot configure"
                                        .format(args.search_pattern))

            if host_info['server_type'] != args.type:
                raise YBOpsRuntimeError("Instance: {} is of type {}, not {}, cannot configure"
                                        .format(args.search_pattern,
                                                host_info['server_type'],
                                                args.type))
            self.update_ansible_vars_with_host_info(host_info, args.custom_ssh_port)
            # If we have a package, then manually copy it over using scp instead of going over
            # ansible, so we do not have issues such as ENG-3424.
            # Python based paramiko seemed to have the same problems as ansible copy module!
            #
            # NOTE: we should only do this if we have to download the package...
            # NOTE 2: itest should download package from s3 to improve speed for instances in AWS.
            # TODO: Add a variable to specify itest ssh_user depending on VM users.
            start_time = time.time()
            if args.package and (args.tags is None or "download-software" in args.tags):
                if args.s3_remote_download:
                    aws_access_key = args.aws_access_key or os.getenv('AWS_ACCESS_KEY_ID')
                    aws_secret_key = args.aws_secret_key or os.getenv('AWS_SECRET_ACCESS_KEY')

                    if aws_access_key is None or aws_secret_key is None:
                        raise YBOpsRuntimeError("Aws credentials are not specified, nor found in " +
                                                "the environment to download YB package from {}"
                                                .format(args.package))

                    s3_uri_pattern = r"^s3:\/\/(?:[^\/]+)\/(?:.+)$"
                    match = re.match(s3_uri_pattern, args.package)
                    if not match:
                        raise YBOpsRuntimeError("{} is not a valid s3 URI. Must match {}"
                                                .format(args.package, s3_uri_pattern))

                    self.extra_vars['s3_package_path'] = args.package
                    self.extra_vars['aws_access_key'] = aws_access_key
                    self.extra_vars['aws_secret_key'] = aws_secret_key
                    logging.info(
                        "Variables to download {} directly on the remote host added."
                        .format(args.package))
                elif args.gcs_remote_download:
                    gcs_credentials_json = args.gcs_credentials_json or \
                        os.getenv('GCS_CREDENTIALS_JSON')

                    if gcs_credentials_json is None:
                        raise YBOpsRuntimeError("GCS credentials are not specified, nor found in " +
                                                "the environment to download YB package from {}"
                                                .format(args.package))

                    gcs_uri_pattern = r"^gs:\/\/(?:[^\/]+)\/(?:.+)$"
                    match = re.match(gcs_uri_pattern, args.package)
                    if not match:
                        raise YBOpsRuntimeError("{} is not a valid gs URI. Must match {}"
                                                .format(args.package, gcs_uri_pattern))

                    self.extra_vars['gcs_package_path'] = args.package
                    self.extra_vars['gcs_credentials_json'] = gcs_credentials_json
                    logging.info(
                        "Variables to download {} directly on the remote host added."
                        .format(args.package))
                elif args.http_remote_download:
                    http_url_pattern = r"^((?:https?):\/\/(?:www\.)?[a-z0-9\.:].*?)(?:\?.*)?$"
                    match = re.match(http_url_pattern, args.package)
                    if not match:
                        raise YBOpsRuntimeError("{} is not a valid HTTP URL. Must match {}"
                                                .format(args.package, http_url_pattern))

                    # Remove query string part from http url.
                    self.extra_vars["package"] = match.group(1)
                    # Pass the complete http url to download the package.
                    self.extra_vars['http_package_path'] = match.group(0)
                    self.extra_vars['http_package_checksum'] = args.http_package_checksum
                    logging.info(
                        "Variables to download {} directly on the remote host added."
                        .format(args.package))

                elif args.itest_s3_package_path and args.type == self.YB_SERVER_TYPE:
                    itest_extra_vars = self.extra_vars.copy()
                    itest_extra_vars["itest_s3_package_path"] = args.itest_s3_package_path
                    itest_extra_vars["ssh_user"] = "centos"
                    # Runs all itest-related tasks (e.g. download from s3 bucket).
                    itest_extra_vars["tags"] = "itest"
                    self.cloud.setup_ansible(args).run(
                        "configure-{}.yml".format(args.type), itest_extra_vars,
                        host_info)
                    logging.info(("[app] Running itest tasks including S3 " +
                                  "package download {} to {} took {:.3f} sec").format(
                        args.itest_s3_package_path,
                        args.search_pattern, time.time() - start_time))
                else:
                    if copy_to_tmp(self.extra_vars, args.package,
                                   remote_tmp_dir=args.remote_tmp_dir):
                        raise YBOpsRecoverableError(
                            f"[app] Failed to copy package {args.package} to {args.search_pattern}")

                    logging.info("[app] Copying package {} to {} took {:.3f} sec".format(
                        args.package, args.search_pattern, time.time() - start_time))

            if args.ybc_package is not None:
                ybc_package_path = args.ybc_package
                if os.path.isfile(ybc_package_path):
                    start_time = time.time()
                    if copy_to_tmp(self.extra_vars, ybc_package_path,
                                   remote_tmp_dir=args.remote_tmp_dir):
                        raise YBOpsRecoverableError(f"[app] Failed to copy package "
                                                    f"{ybc_package_path} to {args.search_pattern}")
                    logging.info("[app] Copying package {} to {} took {:.3f} sec".format(
                        ybc_package_path, args.search_pattern, time.time() - start_time))

        if args.install_third_party_packages:
            if args.local_package_path:
                self.extra_vars.update({"local_package_path": args.local_package_path})
            else:
                logging.warn("Local Directory Tarball Path not specified skipping")
                return
            self.cloud.setup_ansible(args).run(
                "install-third-party.yml", self.extra_vars, host_info)
            return

        rotate_certs = False
        if args.cert_rotate_action is not None:
            if args.cert_rotate_action == "APPEND_NEW_ROOT_CERT":
                self.cloud.append_new_root_cert(
                    self.extra_vars,
                    args.root_cert_path,
                    args.certs_location,
                    args.certs_node_dir
                )
                return
            if args.cert_rotate_action == "REMOVE_OLD_ROOT_CERT":
                self.cloud.remove_old_root_cert(
                    self.extra_vars,
                    args.certs_node_dir
                )
                return
            if args.cert_rotate_action == "ROTATE_CERTS":
                rotate_certs = True
                # Clean up client certs to remove old cert traces
                self.cloud.cleanup_client_certs(self.extra_vars)

        # Copying Server Certs
        logging.info("Copying certificates to {}.".format(args.search_pattern))
        if args.root_cert_path is not None:
            logging.info("Server RootCA Certificate Exists: {}.".format(args.root_cert_path))
            self.cloud.copy_server_certs(
                self.extra_vars,
                args.root_cert_path,
                args.server_cert_path,
                args.server_key_path,
                args.certs_location,
                args.certs_node_dir,
                rotate_certs,
                args.skip_cert_validation)

        if args.root_cert_path_client_to_server is not None:
            logging.info("Server clientRootCA Certificate Exists: {}.".format(
                args.root_cert_path_client_to_server))
            self.cloud.copy_server_certs(
                self.extra_vars,
                args.root_cert_path_client_to_server,
                args.server_cert_path_client_to_server,
                args.server_key_path_client_to_server,
                args.certs_location_client_to_server,
                args.certs_client_dir,
                rotate_certs,
                args.skip_cert_validation)

        # Copying client certs
        if args.client_cert_path is not None:
            logging.info("Client Certificate Exists: {}.".format(args.client_cert_path))
            if args.root_cert_path_client_to_server is not None:
                self.cloud.copy_client_certs(
                    self.extra_vars,
                    args.root_cert_path_client_to_server,
                    args.client_cert_path,
                    args.client_key_path,
                    args.certs_location_client_to_server
                )
            else:
                self.cloud.copy_client_certs(
                    self.extra_vars,
                    args.root_cert_path,
                    args.client_cert_path,
                    args.client_key_path,
                    args.certs_location
                )

        if args.local_gflag_files_path is not None and args.remote_gflag_files_path is not None:
            # Copy all the files from local gflags file path to remote
            files = os.listdir(args.local_gflag_files_path)
            remote_shell = RemoteShell(self.extra_vars)
            # Delete the gFlag file directory in case already present in remote
            remote_shell.exec_command("rm -rf {}".format(args.remote_gflag_files_path))
            # Create the gFlag file directory before copying the file.
            remote_shell.exec_command("mkdir -p {}".format(args.remote_gflag_files_path))
            for file in files:
                src_file = os.path.join(args.local_gflag_files_path, file)
                dest_file = os.path.join(args.remote_gflag_files_path, file)
                remote_shell.put_file(src_file, dest_file)
            # Clean up the local gflag file directory
            try:
                os.rmdir(args.local_gflag_files_path)
            except OSError as e:
                logging.info("[app] Deletion of local gflag directory failed with {}".format(e))

        if args.encryption_key_source_file is not None:
            self.extra_vars["encryption_key_file"] = args.encryption_key_source_file
            logging.info("Copying over encryption-at-rest certificate from {} to {}".format(
                args.encryption_key_source_file, args.encryption_key_target_dir))
            self.cloud.create_encryption_at_rest_file(self.extra_vars, self.extra_vars)

        if args.reset_master_state and args.extra_gflags is not None:
            delete_paths = []
            extra_gflags = self.extra_vars.get('extra_gflags')
            if extra_gflags is not None:
                fs_data_dirs_str = extra_gflags.get('fs_data_dirs')
                if fs_data_dirs_str is not None:
                    fs_data_dirs = fs_data_dirs_str.split(",")
                    for fs_data_dir in fs_data_dirs:
                        for state_dir in self.MASTER_STATE_DIRS:
                            # Example path is /mnt/d0/yb-data/master/wals.
                            path = os.path.join(fs_data_dir, 'yb-data', 'master', state_dir)
                            delete_paths.append(path)
            if delete_paths:
                self.extra_vars["delete_paths"] = delete_paths
        # If we are just rotating certs, we don't need to do any configuration changes.
        if not rotate_certs:
            self.cloud.setup_ansible(args).run(
                "configure-{}.yml".format(args.type), self.extra_vars, host_info)


class InitYSQLMethod(AbstractInstancesMethod):

    def __init__(self, base_command):
        super(InitYSQLMethod, self).__init__(base_command, "initysql")

    def add_extra_args(self):
        super(InitYSQLMethod, self).add_extra_args()
        self.parser.add_argument("--master_addresses", required=True,
                                 help="host:port csv of tserver's masters.")

    def callback(self, args):
        self.update_ansible_vars_with_args(args)
        connect_options = {}
        connect_options.update(self.extra_vars)
        # TODO: replace with args.ssh_user when it's setup in the flow
        connect_options.update({
            "ssh_user": "yugabyte",
            "node_agent_user": "yugabyte"
        })
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance: {} does not exist, cannot call initysql".format(
                                    args.search_pattern))
        connect_options.update(get_ssh_host_port(host_info, args.custom_ssh_port))
        logging.info("Initializing YSQL on Instance: {}".format(args.search_pattern))
        self.cloud.initYSQL(args.master_addresses, connect_options, args)


class ControlInstanceMethod(AbstractInstancesMethod):
    def get_ssh_user(self):
        # Force control instances to use the "yugabyte" user.
        return "yugabyte"

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance: {} does not exist, cannot run ctl commands"
                                    .format(args.search_pattern))

        if host_info['server_type'] != self.YB_SERVER_TYPE:
            raise YBOpsRuntimeError("Instance: {} is of type {}, not {}, cannot configure".format(
                args.search_pattern, host_info['server_type'],
                self.YB_SERVER_TYPE))

        # Skip if instance is not running and command is stopping process.
        if not host_info.get("is_running", True) and self.name == "stop":
            logging.info(
                "Skipping ctl command %s for process: %s due to node not in running state",
                self.name, self.base_command.name)
            return

        logging.info("Running ctl command {} for process: {} in instance: {}".format(
            self.name, self.base_command.name, args.search_pattern))

        self.update_ansible_vars_with_args(args)
        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port))
        self.cloud.run_control_script(
            self.base_command.name, self.name, args, self.extra_vars, host_info)


class AbstractVaultMethod(AbstractMethod):
    def __init__(self, base_command, method_name):
        super(AbstractVaultMethod, self).__init__(base_command, method_name)
        self.cluster_vault = dict()

    def add_extra_args(self):
        super(AbstractVaultMethod, self).add_extra_args()
        self.parser.add_argument("--private_key_file", required=True, help="Private key filename.")
        self.parser.add_argument("--has_sudo_password", action="store_true", help="sudo password.")
        self.parser.add_argument("--vault_file", required=False, help="Vault filename.")


class AccessCreateVaultMethod(AbstractVaultMethod):
    def __init__(self, base_command):
        super(AccessCreateVaultMethod, self).__init__(base_command, "create-vault")

    def callback(self, args):
        file_prefix = os.path.splitext(args.private_key_file)[0]

        try:
            if args.vault_password_file is None:
                vault_password = generate_random_password()
                args.vault_password_file = "{}.vault_password".format(file_prefix)
                with open(args.vault_password_file, "w") as f:
                    f.write(vault_password)
            elif os.path.exists(args.vault_password_file):
                with open(args.vault_password_file, "r") as f:
                    vault_password = f.read().strip()

                if vault_password is None:
                    raise YBOpsRuntimeError("Unable to read {}".format(args.vault_password_file))
            else:
                raise YBOpsRuntimeError("Vault password file doesn't exist.")

            if args.vault_file is None:
                args.vault_file = "{}.vault".format(file_prefix)

            rsa_key = validated_key_file(args.private_key_file)
        except Exception:
            self._cleanup_dir(os.path.dirname(args.private_key_file))
            raise

        # TODO: validate if the file provided is actually a private key file or not.
        public_key = format_rsa_key(rsa_key, public_key=True)
        private_key = format_rsa_key(rsa_key, public_key=False)
        self.cluster_vault.update(
            id_rsa=private_key,
            id_rsa_pub=public_key,
            authorized_keys=public_key
        )

        # These are saved for itest specific improvements.
        aws_access_key = os.environ.get('AWS_ACCESS_KEY_ID', "")
        aws_secret = os.environ.get('AWS_SECRET_ACCESS_KEY', "")
        if aws_access_key and aws_secret:
            self.cluster_vault.update(
                AWS_ACCESS_KEY_ID=os.environ['AWS_ACCESS_KEY_ID'],
                AWS_SECRET_ACCESS_KEY=os.environ['AWS_SECRET_ACCESS_KEY']
            )

        vault_data = dict(cluster_server_vault=self.cluster_vault)
        if args.has_sudo_password:
            sudo_password = getpass.getpass("SUDO Password: ")
            vault_data.update({"ansible_become_pass": sudo_password})

        vault = Vault(vault_password)
        vault.dump(vault_data, open(args.vault_file, 'w'))
        print(json.dumps({
            "vault_file": args.vault_file,
            "vault_password": args.vault_password_file
        }))


class AccessEditVaultMethod(AbstractVaultMethod):
    def __init__(self, base_command):
        super(AccessEditVaultMethod, self).__init__(base_command, "edit-vault")

    def callback(self, args):
        file_prefix = os.path.splitext(args.private_key_file)[0]
        args.vault_password_file = args.vault_password_file or \
            "{}.vault_password".format(file_prefix)
        if os.path.exists(args.vault_password_file):
            with open(args.vault_password_file, "r") as f:
                vault_password = f.read().strip()

            if vault_password is None:
                raise YBOpsRuntimeError("Unable to read {}".format(args.vault_password_file))
        else:
            raise YBOpsRuntimeError("Vault password file doesn't exists.")

        if args.vault_file is None:
            args.vault_file = "{}.vault".format(file_prefix)

        vault = Vault(vault_password)
        data = vault.load(open(args.vault_file).read())

        if args.has_sudo_password:
            if not YB_SUDO_PASS:
                raise YBOpsRuntimeError("Did not find sudo password.")
            data['ansible_become_pass'] = YB_SUDO_PASS

        vault.dump(data, open(args.vault_file, 'w'))
        print(json.dumps({
            "vault_file": args.vault_file,
            "vault_password": args.vault_password_file
        }))


class AbstractAccessMethod(AbstractMethod):
    def __init__(self, base_command, control_command):
        super(AbstractAccessMethod, self).__init__(base_command, control_command)

    def add_extra_args(self):
        super(AbstractAccessMethod, self).add_extra_args()
        self.parser.add_argument("--key_pair_name", required=True, help="Key Pair name.")
        self.parser.add_argument("--key_file_path", required=True, help="Key file path.")
        self.parser.add_argument("--public_key_file", required=False, help="Public key filename.")
        self.parser.add_argument("--private_key_file", required=False, help="Private key filename.")
        self.parser.add_argument("--delete_remote", action="store_true", help="Delete from cloud.")
        self.parser.add_argument("--skip_add_keypair_aws", action="store_true", default=False,
                                 help="Skip Access Key Upload for AWS Region")
        self.parser.add_argument("--ignore_auth_failure", action="store_true",
                                 help="Ignore cloud auth failure.")

    def validate_key_files(self, args):
        public_key_file = args.public_key_file
        private_key_file = args.private_key_file
        if not public_key_file and not private_key_file:
            # We need to generate a public/private key file
            (private_key_file, public_key_file) = generate_rsa_keypair(args.key_pair_name,
                                                                       args.key_file_path)
            # update the args with newly generated public key file
            args.public_key_file = os.path.basename(public_key_file)
        elif public_key_file:
            key_file_name = os.path.splitext(public_key_file)[0]
            private_key_file = "{}.pem".format(key_file_name)
            # Just additional check to validated if we have .pem file for the public key
            # If not we would be in big trouble :|
            if not os.path.exists(os.path.join(args.key_file_path, private_key_file)):
                raise YBOpsRuntimeError("Private key file not found {}".format(private_key_file))

        return (private_key_file, public_key_file)


class AbstractNetworkMethod(AbstractMethod):
    def __init__(self, base_command, method_name):
        super(AbstractNetworkMethod, self).__init__(base_command, method_name)

    def add_extra_args(self):
        super(AbstractNetworkMethod, self).add_extra_args()
        self.parser.add_argument("--metadata_override", required=False,
                                 help="A custom YML metadata override file.")

    def preprocess_args(self, args):
        super(AbstractNetworkMethod, self).preprocess_args(args)
        if args.metadata_override:
            self.cloud.update_metadata(args.metadata_override)


class AccessDeleteKeyMethod(AbstractAccessMethod):
    def __init__(self, base_command):
        super(AccessDeleteKeyMethod, self).__init__(base_command, "delete-key")

    def _delete_key_pair(self, args):
        pass

    def callback(self, args):
        try:
            if (args.delete_remote):
                self._delete_key_pair(args)
            self._cleanup_dir(args.key_file_path)
            print(json.dumps({"success": "Keypair {} deleted.".format(args.key_pair_name)}))
        except Exception as e:
            logging.error(e)
            print(json.dumps({"error": "Unable to delete Keypair: {}".format(args.key_pair_name)}))


class TransferXClusterCerts(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(TransferXClusterCerts, self).__init__(base_command, "transfer_xcluster_certs")
        self.ssh_user = "yugabyte"  # Default ssh username.

    def add_extra_args(self):
        super(TransferXClusterCerts, self).add_extra_args()
        self.parser.add_argument("--root_cert_path",
                                 help="The path to the root cert of the source universe on "
                                      "the Platform host.")
        self.parser.add_argument("--replication_config_name",
                                 required=True,
                                 help="The format of this name must be "
                                      "[Source universe UUID]_[Config name].")
        self.parser.add_argument("--producer_certs_dir",
                                 required=True,
                                 help="The directory containing the certs on the target universe.")
        self.parser.add_argument("--action",
                                 default="copy",
                                 help="If true, the root certificate will be removed.")

    def _verify_params(self, args):
        if len(args.replication_config_name.split("_", 1)) != 2:
            raise YBOpsRuntimeError(
                "--replication_config_name {} is not valid. It must have " +
                "[Source universe UUID]_[Config name] format"
                .format(args.replication_config_name))

    def callback(self, args):
        self._verify_params(args)
        if args.ssh_user is not None:
            self.ssh_user = args.ssh_user
        host_info = self.cloud.get_host_info(args)
        # Populate extra_vars.
        self.update_ansible_vars_with_args(args)
        connect_options = {}
        connect_options.update(self.extra_vars)
        connect_options.update({
            "ssh_user": self.ssh_user,
            "node_agent_user": self.ssh_user
        })
        connect_options.update(self.get_server_host_port(host_info, args.custom_ssh_port))

        # TODO: Add support for rotate certs
        if args.action == "copy":
            if args.root_cert_path is None:
                raise YBOpsRuntimeError("--root_cert_path argument is missing")
            self.cloud.copy_xcluster_root_cert(
                connect_options,
                args.root_cert_path,
                args.replication_config_name,
                args.producer_certs_dir)
        elif args.action == "remove":
            self.cloud.remove_xcluster_root_cert(
                connect_options,
                args.replication_config_name,
                args.producer_certs_dir)
        else:
            raise YBOpsRuntimeError("The action \"{}\" was not found: Must be either copy, "
                                    "or remove".format(args.action))


class RebootInstancesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(RebootInstancesMethod, self).__init__(base_command, "reboot")

    def add_extra_args(self):
        super().add_extra_args()
        # TODO correct it to use --use_reboot_command.
        self.parser.add_argument("--use_ssh", action='store_true', default=False,
                                 help="Use 'sudo reboot' instead of cloud provider SDK.")

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Could not find host {} to reboot".format(
                args.search_pattern))
        # If key exists in dictionary, it means not an on-prem reboot.
        # If key doesn't exist in dict, manually provisioned on-prem reboots are
        # disallowed at the API layer.
        if 'is_running' in host_info.keys() and not host_info.get('is_running'):
            raise YBOpsRuntimeError("Host must be running to be rebooted, currently in '{}' state"
                                    .format(host_info.get('instance_state')))
        logging.info("Rebooting instance {}".format(args.search_pattern))

        # Get Sudo SSH User
        ssh_user = args.ssh_user
        if ssh_user is None:
            ssh_user = DEFAULT_SSH_USER

        # Populate extra_vars.
        self.update_ansible_vars_with_args(args)
        if args.use_ssh:
            self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port,
                                                             default_port=True))
            self.extra_vars.update({
                "ssh_user": ssh_user,
                "node_agent_user": ssh_user
            })
            self.update_open_ssh_port(args)
            _, _, stderr = remote_exec_command(self.extra_vars, 'sudo reboot')
            # Cannot rely on rc, as for reboot script won't exit gracefully,
            # & we will be returned -1.
            if (isinstance(stderr, list) and len(stderr) > 0):
                raise YBOpsRecoverableError(f"Failed to connect to {args.search_pattern}")
        else:
            server_ports = self.get_server_ports_to_check(args)
            self.cloud.reboot_instance(host_info, server_ports)
        self.wait_for_host(args, False)


class HardRebootInstancesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(HardRebootInstancesMethod, self).__init__(base_command, "hard_reboot")

    def add_extra_args(self):
        super(HardRebootInstancesMethod, self).add_extra_args()

    def callback(self, args):
        instance = self.cloud.get_host_info(args)
        if not instance:
            raise YBOpsRuntimeError("Could not find host {} to hard reboot".format(
                args.search_pattern))
        host_info = vars(args)
        host_info.update(instance)
        instance_state = host_info['instance_state']
        if instance_state not in self.valid_states:
            raise YBOpsRuntimeError("Instance is in invalid state '{}' for attempting a hard reboot"
                                    .format(instance_state))
        if instance_state in self.valid_stoppable_states:
            logging.info("Stopping instance {}".format(args.search_pattern))
            self.cloud.stop_instance(host_info)
        logging.info("Starting instance {}".format(args.search_pattern))
        self.update_ansible_vars_with_args(args)
        if args.boot_script is not None and self.cloud.name == "gcp":
            # GCP executes boot_script as part of each boot, pause/resume.
            self.cloud.update_user_data(args)
        server_ports = self.get_server_ports_to_check(args)
        self.cloud.start_instance(host_info, server_ports)


class RunHooks(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(RunHooks, self).__init__(base_command, "runhooks")

    def add_extra_args(self):
        super(RunHooks, self).add_extra_args()
        self.parser.add_argument("--execution_lang", required=True,
                                 help="The execution language to use.")
        self.parser.add_argument("--trigger", required=True,
                                 help="The event that triggered this hook to run.")
        self.parser.add_argument("--hook_path", required=True,
                                 help="The path to the script on the Anywhere instance.")
        self.parser.add_argument("--use_sudo", action="store_true", default=False,
                                 help="Use superuser privileges while executing custom hook.")
        self.parser.add_argument("--parent_task", required=True,
                                 help="The parent task running the hook.")
        self.parser.add_argument("--runtime_args", help="The parent task running the hook.")

    def _verify_params(self, args):
        if args.execution_lang not in ['Bash', 'Python']:
            raise YBOpsRuntimeError("--execution_lang {} is not valid. Must be Bash or Python",
                                    args.execution_lang)

    def get_exec_command(self, args):
        dest_path = os.path.join(args.remote_tmp_dir, os.path.basename(args.hook_path))
        lang_command = args.execution_lang.lower()

        cmd = "sudo " if args.use_sudo else ""
        cmd += "{} {} --parent_task {} --trigger {}".format(lang_command, dest_path,
                                                            args.parent_task, args.trigger)

        # Add extra runtime arguments
        if args.runtime_args is not None:
            runtime_args_json = json.loads(args.runtime_args)
            for key, value in runtime_args_json.items():
                cmd += " --{} {}".format(key, value)

        return cmd

    def callback(self, args):
        self._verify_params(args)
        ssh_user = "yugabyte"
        use_default_ssh_port = args.trigger == 'PreNodeProvision'

        # Use the SSH user if:
        # 1. Sudo permissions are needed
        # 2. Before provisioning, since the yugabyte user has not been created
        if args.use_sudo or args.trigger == 'PreNodeProvision':
            if args.ssh_user is not None:
                ssh_user = args.ssh_user
            else:
                ssh_user = DEFAULT_SSH_USER

        self.update_ansible_vars_with_args(args)
        host_info = self.cloud.get_host_info(args)
        self.extra_vars.update(
            self.get_server_host_port(
                host_info, args.custom_ssh_port, use_default_ssh_port))
        self.extra_vars.update({
            "ssh_user": ssh_user,
            "node_agent_user": ssh_user
        })

        self.wait_for_host(args, use_default_ssh_port)

        # Copy the hook to the remote node
        scp_result = copy_to_tmp(self.extra_vars, args.hook_path,
                                 remote_tmp_dir=args.remote_tmp_dir)
        if scp_result:
            raise YBOpsRuntimeError("Could not transfer hook to target node.")

        # Execute hook on remote node
        rc, _, stderr = remote_exec_command(self.extra_vars, self.get_exec_command(args))
        if rc:
            raise YBOpsRuntimeError("Failed running custom hook:\n" + ''.join(stderr))

        # Delete custom hook
        remove_command = "rm " + os.path.join(args.remote_tmp_dir, os.path.basename(args.hook_path))
        rc, _, stderr = remote_exec_command(self.extra_vars, remove_command)
        if rc:
            logging.warn("Failed deleting custom hook:\n" + ''.join(stderr))


class WaitForConnection(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(WaitForConnection, self).__init__(base_command, "wait_for_connection", True)

    def add_extra_args(self):
        super(WaitForConnection, self).add_extra_args()

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        normalized_state = self.cloud.normalize_instance_state(host_info.get("instance_state"))
        logging.info("Host {} is in normalized state {}({})."
                     .format(args.search_pattern, normalized_state,
                             host_info.get("instance_state")))
        # Onprem nodes report unknown state.
        if normalized_state not in (InstanceState.UNKNOWN, InstanceState.STARTING,
                                    InstanceState.RUNNING):
            raise YBOpsRecoverableError("Host {} is in invalid state {}."
                                        .format(args.search_pattern,
                                                host_info.get("instance_state")))
        # Update the ansible args (particularly connection params).
        self.update_ansible_vars_with_args(args)
        # Set the host and default port.
        self.extra_vars.update(
            self.get_server_host_port(host_info, args.custom_ssh_port, default_port=True))
        # Update with the open port.
        self.update_open_ssh_port(args)
        # Port is already open. Wait for ssh to succeed.
        connected = wait_for_server(self.extra_vars)
        if not connected:
            host_port_user = get_host_port_user(self.extra_vars)
            raise YBOpsRecoverableError("Connection({}) to host {} by user {} failed at port {}"
                                        .format(host_port_user["connection_type"],
                                                host_port_user["host"],
                                                host_port_user["user"],
                                                host_port_user["port"]))


class ManageOtelCollector(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(ManageOtelCollector, self).__init__(base_command, "manage_otel_collector", True)

    def add_extra_args(self):
        super(ManageOtelCollector, self).add_extra_args()

        self.parser.add_argument("--install_otel_collector", action="store_true")
        self.parser.add_argument('--otel_col_config_file', default=None,
                                 help="Path to OpenTelemetry Collector config file.")
        self.parser.add_argument('--otel_col_aws_access_key', default=None,
                                 help="AWS Access Key used for logs export")
        self.parser.add_argument('--otel_col_aws_secret_key', default=None,
                                 help="AWS Secret Key used for logs export.")
        self.parser.add_argument('--otel_col_gcp_creds_file', default=None,
                                 help="Path to GCP credentials file used for logs export.")
        self.parser.add_argument('--ycql_audit_log_level', default=None,
                                 help="YCQL audit log level.")
        self.parser.add_argument("--local_package_path",
                                 required=False,
                                 help="Path to local directory with third party software tarballs.")

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Could not find host {} to provision!".format(
                args.search_pattern))

        # Update the ansible args (particularly connection params).
        self.update_ansible_vars_with_args(args)
        # Set the host and default port.
        self.extra_vars.update(
            self.get_server_host_port(host_info, args.custom_ssh_port, default_port=True))
        # Update with the open port.
        self.update_open_ssh_port(args)

        if args.local_package_path:
            self.extra_vars.update({"local_package_path": args.local_package_path})
        if args.install_otel_collector:
            self.extra_vars.update({"install_otel_collector": args.install_otel_collector})
        if args.otel_col_config_file:
            self.extra_vars.update({"otel_col_config_file_local": args.otel_col_config_file})
        if args.otel_col_aws_access_key:
            self.extra_vars.update({"otel_col_aws_access_key": args.otel_col_aws_access_key})
        if args.otel_col_aws_secret_key:
            self.extra_vars.update({"otel_col_aws_secret_key": args.otel_col_aws_secret_key})
        if args.otel_col_gcp_creds_file:
            self.extra_vars.update({"otel_col_gcp_creds_local": args.otel_col_gcp_creds_file})
        if args.ycql_audit_log_level:
            self.extra_vars.update({"ycql_audit_log_level": args.ycql_audit_log_level})

        if wait_for_server(self.extra_vars):
            self.cloud.setup_ansible(args).run("yb-otel-collector.yml",
                                               self.extra_vars, host_info)
        else:
            host_port_user = get_host_port_user(self.extra_vars)
            raise YBOpsRecoverableError("Could not connect({}) into node {}:{} using username {}"
                                        .format(host_port_user["connection_type"],
                                                host_port_user["host"],
                                                host_port_user["user"],
                                                host_port_user["port"]))
