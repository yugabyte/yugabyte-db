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

from ybops.common.exceptions import YBOpsRuntimeError
from ybops.utils import get_ssh_host_port, wait_for_ssh, get_path_from_yb, \
  generate_random_password, validated_key_file, format_rsa_key, validate_cron_status, \
  YB_SUDO_PASS, DEFAULT_MASTER_HTTP_PORT, DEFAULT_MASTER_RPC_PORT, DEFAULT_TSERVER_HTTP_PORT, \
  DEFAULT_TSERVER_RPC_PORT, DEFAULT_CQL_PROXY_RPC_PORT, DEFAULT_REDIS_PROXY_RPC_PORT, \
  DEFAULT_SSH_USER
from ansible_vault import Vault
from ybops.utils import generate_rsa_keypair, scp_to_tmp


class ConsoleLoggingErrorHandler(object):
    def __init__(self, cloud):
        self.cloud = cloud

    def __call__(self, exception, args):
        if args.search_pattern:
            console_output = self.cloud.get_console_output(args)

            if console_output:
                logging.error("Dumping latest console output for {}:".format(args.search_pattern))
                logging.error(console_output)


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

    def preprocess_args(self, args):
        """Hook for pre-processing args before actually executing the callback. Useful for shared
        default processing on similar methods with a shared abstract superclass.
        """
        logging.debug("...preprocessing {}".format(self.name))

    def callback_wrapper(self, args):
        """Hook for setting up actual command execution.
        """
        logging.debug("...calling {}".format(self.name))
        if self.need_validation:
            self.cloud.validate_credentials()
        self.cloud.init_cloud_api(args)
        self.preprocess_args(args)
        try:
            self.callback(args)
        except BaseException as e:
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
    INSTANCE_LOOKUP_RETRY_LIMIT = 120

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
        self.parser.add_argument('--tags', default=None)
        self.parser.add_argument("--skip_tags", default=None)

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
        self.parser.add_argument("--machine_image",
                                 required=False,
                                 help="The machine image (e.g. an AMI on AWS) to install, "
                                      "this depends on the region.")

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
            "private_key_file": args.private_key_file
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

        if args.instance_tags:
            updated_args["instance_tags"] = json.loads(args.instance_tags)

        self.extra_vars.update(updated_args)

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
        self.extra_vars.update(get_ssh_host_port(host_info, custom_ssh_port))

    def wait_for_host(self, args, default_port=True):
        logging.info("Waiting for instance {}".format(args.search_pattern))
        host_lookup_count = 0
        # Cache the result of the cloud call outside of the loop.
        host_info = None

        while host_lookup_count < self.INSTANCE_LOOKUP_RETRY_LIMIT:
            if not host_info:
                host_info = self.cloud.get_host_info(args)

            if host_info:
                self.extra_vars.update(
                    get_ssh_host_port(host_info, args.custom_ssh_port, default_port=default_port))
                if wait_for_ssh(self.extra_vars["ssh_host"],
                                self.extra_vars["ssh_port"],
                                self.extra_vars["ssh_user"],
                                args.private_key_file):
                    return host_info

            sys.stdout.write('.')
            sys.stdout.flush()
            time.sleep(1)
            host_lookup_count += 1

        raise YBOpsRuntimeError("Timed out waiting for instance: '{0}'".format(
            args.search_pattern))

    # Find the open ssh port and update the dictionary.
    def update_open_ssh_port(self, args):
        ssh_port_updated = False
        ssh_ports = [self.extra_vars["ssh_port"], args.custom_ssh_port]
        ssh_port = self.cloud.wait_for_ssh_ports(
            self.extra_vars["ssh_host"], args.search_pattern, ssh_ports)
        if self.extra_vars["ssh_port"] != ssh_port:
            self.extra_vars["ssh_port"] = ssh_port
            ssh_port_updated = True
        return ssh_port_updated


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
            self.cloud.unmount_disk(host_info, current_root_volume)
            logging.info("==> Root volume {} unmounted from {}".format(
                current_root_volume, id))
            unmounted = True
            logging.info("==> Mounting {} as the new root volume on {}".format(
                args.replacement_disk, id))
            self._mount_root_volume(host_info, args.replacement_disk)
        except Exception as e:
            logging.exception(e)
            if unmounted:
                self._mount_root_volume(host_info, current_root_volume)
        finally:
            self.cloud.start_instance(host_info, 22)

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)

        if not host_info:
            raise YBOpsRuntimeError(
                "Instance {} not found, was it stopped?".format(args.search_pattern))

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
                                 help="The ip of the instance to provision")

        self.parser.add_argument("--assign_static_public_ip",
                                 action="store_true",
                                 default=False,
                                 help="Assign a static public ip to the instance")

        self.parser.add_argument("--boot_disk_size_gb",
                                 type=int,
                                 default=40,
                                 help="Size of the boot disk in GBs. Currently only works on GCP.")

        self.parser.add_argument("--auto_delete_boot_disk",
                                 action="store_true",
                                 default=True,
                                 help="Delete the root volume on VM termination")

        self.parser.add_argument("--boot_script", required=False,
                                 help="Custom boot script to execute on the instance.")

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if host_info:
            raise YBOpsRuntimeError("Host {} already created.".format(args.search_pattern))

        self.extra_vars.update({
            "volume_type": args.volume_type
        })
        self.update_ansible_vars_with_args(args)
        self.run_ansible_create(args)

        if args.boot_script:
            logging.info(
                'Waiting for the startup script to finish on {}'.format(args.search_pattern))

            host_info = get_ssh_host_port(
                self.wait_for_host(args), args.custom_ssh_port, default_port=True)
            host_info['ssh_user'] = DEFAULT_SSH_USER
            retries = 0
            while not self.cloud.wait_for_startup_script(args, host_info) and retries < 5:
                retries += 1
                time.sleep(2 ** retries)

            logging.info('Startup script finished on {}'.format(args.search_pattern))


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
                                 help="Path to local directory with the prometheus tarball.")
        self.parser.add_argument("--node_exporter_port", type=int, default=9300,
                                 help="The port for node_exporter to bind to")
        self.parser.add_argument("--node_exporter_user", default="prometheus")
        self.parser.add_argument("--install_node_exporter", action="store_true")
        self.parser.add_argument('--remote_package_path', default=None,
                                 help="Path to download thirdparty packages "
                                      "for itest. Only for AWS/onprem")
        self.parser.add_argument("--os_name",
                                 required=False,
                                 help="The os name to provision the universe in.",
                                 default=self.DEFAULT_OS_NAME,
                                 type=str.lower)
        self.parser.add_argument("--disable_custom_ssh", action="store_true",
                                 help="Disable running the ansible task for using custom SSH.")
        self.parser.add_argument("--install_python", action="store_true", default=False,
                                 help="Flag to set if host OS needs python installed for Ansible.")
        self.parser.add_argument("--pg_max_mem_mb", type=int, default=0,
                                 help="Max memory for postgress process.")

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Could not find host {} to provision!".format(
                args.search_pattern))

        self.update_ansible_vars_with_args(args)

        self.extra_vars.update(get_ssh_host_port(host_info, args.custom_ssh_port,
                                                 default_port=True))

        # Check if secondary subnet is present. If so, configure it.
        if host_info.get('secondary_subnet'):
            self.cloud.configure_secondary_interface(
                args, self.extra_vars, self.cloud.get_subnet_cidr(args,
                                                                  host_info['secondary_subnet']))

        if not args.skip_preprovision:
            self.preprovision(args)

        self.extra_vars.update(get_ssh_host_port(host_info, args.custom_ssh_port))
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
        self.extra_vars.update({"systemd_option": args.systemd_services})
        self.extra_vars.update({"instance_type": args.instance_type})
        self.extra_vars["device_names"] = self.cloud.get_device_names(args)

        self.cloud.setup_ansible(args).run("yb-server-provision.yml", self.extra_vars, host_info)

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
        ssh_port_updated = self.update_open_ssh_port(args,)
        use_default_port = not ssh_port_updated
        host_info = self.wait_for_host(args, default_port=use_default_port)
        ansible = self.cloud.setup_ansible(args)
        if (args.install_python):
            self.extra_vars["install_python"] = True
        ansible.run("preprovision.yml", self.extra_vars, host_info)

        if not args.disable_custom_ssh and use_default_port:
            ansible.run("use_custom_ssh_port.yml", self.extra_vars, host_info)


class CreateRootVolumesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(CreateRootVolumesMethod, self).__init__(base_command, "create_root_volumes")
        self.create_method = CreateInstancesMethod(self.base_command)

    def add_extra_args(self):
        self.create_method.parser = self.parser
        self.create_method.add_extra_args()

        self.parser.add_argument("--num_disks",
                                 required=False,
                                 default=1,
                                 help="The number of boot disks to allocate in the zone")

    def preprocess_args(self, args):
        super(CreateRootVolumesMethod, self).preprocess_args(args)
        self.create_method.preprocess_args(args)

    def callback(self, args):
        unique_string = ''.join(random.choice(string.ascii_lowercase) for i in range(6))
        args.search_pattern = "{}-".format(unique_string) + args.search_pattern
        vid = self.create_master_volume(args)
        output = [vid]
        num_disks = int(args.num_disks) - 1

        if num_disks > 0:
            output.extend(self.cloud.clone_disk(args, vid, num_disks))

        logging.info("==> Created volumes {}".format(output))
        print(json.dumps(output))


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

    def prepare(self):
        super(UpdateDiskMethod, self).prepare()

    def callback(self, args):
        self.cloud.update_disk(args)
        host_info = self.cloud.get_host_info(args)
        self.update_ansible_vars_with_args(args)
        ssh_options = {
            # TODO: replace with args.ssh_user when it's setup in the flow
            "ssh_user": self.extra_vars["ssh_user"],
            "private_key_file": args.private_key_file
        }
        ssh_options.update(get_ssh_host_port(host_info, args.custom_ssh_port))
        self.cloud.expand_file_system(args, ssh_options)


class ChangeInstanceTypeMethod(AbstractInstancesMethod):
    """Superclass for resizing the instances (instance type) in the given pattern.
    """

    def __init__(self, base_command):
        super(ChangeInstanceTypeMethod, self).__init__(base_command, "change_instance_type")

    def prepare(self):
        super(ChangeInstanceTypeMethod, self).prepare()

    def callback(self, args):
        self._validate_args(args)
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance {} not found".format(args.search_pattern))

        self._resize_instance(args, self._host_info(args, host_info))

    def _validate_args(self, args):
        # Make sure "instance_type" exists in args
        if args.instance_type is None:
            raise YBOpsRuntimeError("instance_type not defined. Please define your intended type"
                                    " using --instance_type argument")

    def _resize_instance(self, args, host_info):
        logging.info("Stopping instance {}".format(args.search_pattern))
        self.cloud.stop_instance(host_info)
        logging.info('Instance {} is stopped'.format(args.search_pattern))

        try:
            # Change instance type
            self._change_instance_type(args, host_info)
            logging.info('Instance {}\'s type changed to {}'
                         .format(args.search_pattern, args.instance_type))
        except Exception as e:
            raise YBOpsRuntimeError('error executing \"instance.modify_attribute\": {}'
                                    .format(repr(e)))
        finally:
            self.cloud.start_instance(host_info, int(args.custom_ssh_port))
            logging.info('Instance {} is started'.format(args.search_pattern))


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
        ssh_options = {
            "ssh_user": self.get_ssh_user(),
            "private_key_file": args.private_key_file
        }
        ssh_options.update(get_ssh_host_port(host_info, args.custom_ssh_port))
        if not args.systemd_services and not validate_cron_status(
                ssh_options['ssh_host'], ssh_options['ssh_port'], ssh_options['ssh_user'],
                ssh_options['private_key_file']):
            raise YBOpsRuntimeError(
                'Failed to find cronjobs on host {}'.format(ssh_options['ssh_host']))


class ConfigureInstancesMethod(AbstractInstancesMethod):
    VALID_PROCESS_TYPES = ['master', 'tserver']
    CERT_ROTATE_ACTIONS = ['APPEND_NEW_ROOT_CERT', 'ROTATE_CERTS',
                           'REMOVE_OLD_ROOT_CERT', 'UPDATE_CERT_DIRS']
    SKIP_CERT_VALIDATION_OPTIONS = ['ALL', 'HOSTNAME']

    def __init__(self, base_command):
        super(ConfigureInstancesMethod, self).__init__(base_command, "configure")
        self.supported_types = [self.YB_SERVER_TYPE]
        self.error_handler = ConsoleLoggingErrorHandler(self.cloud)

    def prepare(self):
        super(ConfigureInstancesMethod, self).prepare()

        self.parser.add_argument('--package', default=None)
        self.parser.add_argument('--yb_process_type', default=None,
                                 choices=self.VALID_PROCESS_TYPES)
        self.parser.add_argument('--extra_gflags', default=None)
        self.parser.add_argument('--gflags', default=None)
        self.parser.add_argument('--gflags_to_remove', default=None)
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

        # Development flag for itests.
        self.parser.add_argument('--itest_s3_package_path',
                                 help="Path to download packages for itest. Only for AWS/onprem.")

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

        # Make sure we set server_type so we pick the right configure.
        self.update_ansible_vars_with_args(args)

        if args.gflags is not None:
            if args.package:
                raise YBOpsRuntimeError("When changing gflags, do not set packages info.")
            self.extra_vars["gflags"] = json.loads(args.gflags)

        if args.package is not None:
            self.extra_vars["package"] = args.package

        if args.extra_gflags is not None:
            self.extra_vars["extra_gflags"] = json.loads(args.extra_gflags)

        if args.gflags_to_remove is not None:
            self.extra_vars["gflags_to_remove"] = json.loads(args.gflags_to_remove)

        if args.root_cert_path is not None:
            self.extra_vars["root_cert_path"] = args.root_cert_path.strip()

        if args.cert_rotate_action is not None:
            if args.cert_rotate_action not in self.CERT_ROTATE_ACTIONS:
                raise YBOpsRuntimeError(
                    "Supported actions for this command are only: {}".format(
                        self.CERT_ROTATE_ACTIONS))

        host_info = None
        if args.search_pattern != 'localhost':
            host_info = self.cloud.get_host_info(args)
            if not host_info:
                raise YBOpsRuntimeError("Instance: {} does not exists, cannot configure"
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
            if args.package and (args.tags is None or args.tags == "download-software"):
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
                        "configure-{}.yml".format(args.type), itest_extra_vars, host_info)
                    logging.info(("[app] Running itest tasks including S3 " +
                                  "package download {} to {} took {:.3f} sec").format(
                        args.itest_s3_package_path,
                        args.search_pattern, time.time() - start_time))
                else:
                    scp_to_tmp(
                        args.package,
                        self.extra_vars["private_ip"],
                        self.extra_vars["ssh_user"],
                        self.extra_vars["ssh_port"],
                        args.private_key_file)
                    logging.info("[app] Copying package {} to {} took {:.3f} sec".format(
                        args.package, args.search_pattern, time.time() - start_time))

        logging.info("Configuring Instance: {}".format(args.search_pattern))
        ssh_options = {
            # TODO: replace with args.ssh_user when it's setup in the flow
            "ssh_user": self.get_ssh_user(),
            "private_key_file": args.private_key_file
        }
        ssh_options.update(get_ssh_host_port(host_info, args.custom_ssh_port))

        rotate_certs = False
        if args.cert_rotate_action is not None:
            if args.cert_rotate_action == "APPEND_NEW_ROOT_CERT":
                self.cloud.append_new_root_cert(
                    ssh_options,
                    args.root_cert_path,
                    args.certs_location,
                    args.certs_node_dir
                )
                return
            if args.cert_rotate_action == "REMOVE_OLD_ROOT_CERT":
                self.cloud.remove_old_root_cert(
                    ssh_options,
                    args.certs_node_dir
                )
                return
            if args.cert_rotate_action == "ROTATE_CERTS":
                rotate_certs = True

        # Copying Server Certs
        logging.info("Copying certificates to {}.".format(args.search_pattern))
        if args.root_cert_path is not None:
            logging.info("Server RootCA Certificate Exists: {}.".format(args.root_cert_path))
            self.cloud.copy_server_certs(
                ssh_options,
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
                ssh_options,
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
                    ssh_options,
                    args.root_cert_path_client_to_server,
                    args.client_cert_path,
                    args.client_key_path,
                    args.certs_location_client_to_server
                )
            else:
                self.cloud.copy_client_certs(
                    ssh_options,
                    args.root_cert_path,
                    args.client_cert_path,
                    args.client_key_path,
                    args.certs_location
                )

        if args.encryption_key_source_file is not None:
            self.extra_vars["encryption_key_file"] = args.encryption_key_source_file
            logging.info("Copying over encryption-at-rest certificate from {} to {}".format(
                args.encryption_key_source_file, args.encryption_key_target_dir))
            self.cloud.create_encryption_at_rest_file(self.extra_vars, ssh_options)

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
                                 help="host:port csv of tserver's masters")

    def callback(self, args):
        ssh_options = {
            # TODO: replace with args.ssh_user when it's setup in the flow
            "ssh_user": "yugabyte",
            "private_key_file": args.private_key_file
        }
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance: {} does not exists, cannot call initysql".format(
                                    args.search_pattern))
        ssh_options.update(get_ssh_host_port(host_info, args.custom_ssh_port))
        logging.info("Initializing YSQL on Instance: {}".format(args.search_pattern))
        self.cloud.initYSQL(args.master_addresses, ssh_options)


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

        logging.info("Running ctl command {} for process: {} in instance: {}".format(
            self.name, self.base_command.name, args.search_pattern))

        self.update_ansible_vars_with_args(args)
        self.cloud.run_control_script(
            self.base_command.name, self.name, args, self.extra_vars, host_info)


class AbstractVaultMethod(AbstractMethod):
    def __init__(self, base_command, method_name):
        super(AbstractVaultMethod, self).__init__(base_command, method_name)
        self.cluster_vault = dict()

    def add_extra_args(self):
        super(AbstractVaultMethod, self).add_extra_args()
        self.parser.add_argument("--private_key_file", required=True, help="Private key filename")
        self.parser.add_argument("--has_sudo_password", action="store_true", help="sudo password")
        self.parser.add_argument("--vault_file", required=False, help="Vault filename")


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
        self.parser.add_argument("--key_pair_name", required=True, help="Key Pair name")
        self.parser.add_argument("--key_file_path", required=True, help="Key file path")
        self.parser.add_argument("--public_key_file", required=False, help="Public key filename")
        self.parser.add_argument("--private_key_file", required=False, help="Private key filename")

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
            self._delete_key_pair(args)
            self._cleanup_dir(args.key_file_path)
            print(json.dumps({"success": "Keypair {} deleted.".format(args.key_pair_name)}))
        except Exception as e:
            logging.error(e)
            print(json.dumps({"error": "Unable to delete Keypair: {}".format(args.key_pair_name)}))
