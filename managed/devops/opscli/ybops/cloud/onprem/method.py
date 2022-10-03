#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from jinja2 import Environment, FileSystemLoader
from ybops.common.exceptions import YBOpsRuntimeError, get_exception_message
from ybops.cloud.common.method import AbstractAccessMethod, AbstractMethod
from ybops.cloud.common.method import AbstractInstancesMethod
from ybops.cloud.common.method import CreateInstancesMethod
from ybops.cloud.common.method import DestroyInstancesMethod
from ybops.cloud.common.method import ProvisionInstancesMethod, ListInstancesMethod
from ybops.utils import validate_instance, get_datafile_path, YB_HOME_DIR, \
                        get_mount_roots, remote_exec_command, \
                        DEFAULT_MASTER_HTTP_PORT, \
                        DEFAULT_MASTER_RPC_PORT, DEFAULT_TSERVER_HTTP_PORT, \
                        DEFAULT_TSERVER_RPC_PORT, DEFAULT_NODE_EXPORTER_HTTP_PORT
from ybops.utils.remote_shell import RemoteShell
from ybops.utils.ssh import wait_for_ssh, scp_to_tmp, get_ssh_host_port, SSH_RETRY_LIMIT_PRECHECK

import json
import logging
import os
import stat
import ybops.utils as ybutils
from six import iteritems


class OnPremCreateInstancesMethod(CreateInstancesMethod):
    """Subclass for creating instances in an on premise deployment. This is responsible for setting
    up custom metadata required for properly provisioning the machine as part of a certain cluster.
    """

    def __init__(self, base_command):
        super(OnPremCreateInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        # Since in an on premise deployment, the machine should already be created, we use this
        # step purely to validate that we can access the host.
        #
        # TODO: do we still want/need to change to the custom ssh port?
        self.update_ansible_vars_with_args(args)
        self.wait_for_host(args)


class OnPremProvisionInstancesMethod(ProvisionInstancesMethod):
    """Subclass for provisioning instances in an on premise deployment. Sets up the proper Create
    method.
    """

    def __init__(self, base_command):
        super(OnPremProvisionInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        # For onprem, we are always using pre-existing hosts!
        args.skip_preprovision = True
        super(OnPremProvisionInstancesMethod, self).callback(args)


class OnPremValidateMethod(AbstractInstancesMethod):
    """Subclass for verifying the instances in an on premise deployment can be accessed over SSH
    and have all the proper properties. E.g. OS is centos-7, mount path for drives is valid, etc.
    """

    def __init__(self, base_command):
        super(OnPremValidateMethod, self).__init__(base_command, "validate")

    def callback(self, args):
        """args.search_pattern should be a private ip address for the device for OnPrem.
        """
        self.extra_vars.update(
            get_ssh_host_port({"private_ip": args.search_pattern}, args.custom_ssh_port))
        print(validate_instance(self.extra_vars["ssh_host"],
                                self.extra_vars["ssh_port"],
                                self.SSH_USER,
                                args.private_key_file,
                                self.mount_points.split(','),
                                ssh2_enabled=args.ssh2_enabled
                                ))


class OnPremListInstancesMethod(ListInstancesMethod):
    """Subclass for listing instances in onprem.
    """
    def __init__(self, base_command):
        super(OnPremListInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        logging.debug("Received args {}".format(args))

        host_infos = self.cloud.get_host_info(args, get_all=args.as_json)
        if not host_infos:
            return None

        if 'server_type' in host_infos and host_infos['server_type'] is None:
            del host_infos['server_type']

        if args.mount_points:
            for host_info in host_infos:
                try:
                    ssh_options = {
                        "ssh_user": host_info['ssh_user'],
                        "private_key_file": args.private_key_file,
                        "ssh2_enabled": args.ssh2_enabled
                    }
                    ssh_options.update(get_ssh_host_port(
                                        self.cloud.get_host_info(args),
                                        args.custom_ssh_port))
                    host_info['mount_roots'] = get_mount_roots(ssh_options, args.mount_points)

                except Exception as e:
                    logging.info("Error {} locating mount root for host '{}', ignoring.".format(
                        str(e), host_info
                    ))
                    continue

        if args.as_json:
            print(json.dumps(host_infos))
        else:
            print('\n'.join(["{}={}".format(k, v) for k, v in iteritems(host_infos)]))


class OnPremDestroyInstancesMethod(DestroyInstancesMethod):
    """Subclass for destroying an onprem instance, which essentially means cleaning up the used
    resources.
    """
    def __init__(self, base_command):
        super(OnPremDestroyInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(OnPremDestroyInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--install_node_exporter", action="store_true",
                                 help='Check if node exporter should be stopped.')

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            logging.error("Host {} does not exists.".format(args.search_pattern))
            return

        # Run non-db related tasks.
        self.update_ansible_vars_with_args(args)
        if args.install_node_exporter:
            logging.info(("[app] Running control script stop " +
                          "against thirdparty services at {}").format(host_info['name']))
            self.cloud.run_control_script(
                "thirdparty", "stop-services", args, self.extra_vars, host_info)

        self.cloud.run_control_script(
            "platform-services", "stop-services", args, self.extra_vars, host_info)

        # Force db-related commands to use the "yugabyte" user.
        args.ssh_user = "yugabyte"
        self.update_ansible_vars_with_args(args)
        servers = ["master", "tserver"]
        commands = ["stop", "clean", "clean-logs"]
        logging.info(("[app] Running control script stop+clean+clean-logs " +
                     "against master+tserver at {}").format(host_info['name']))
        for s in servers:
            for c in commands:
                self.cloud.run_control_script(
                    s, c, args, self.extra_vars, host_info)

        # Now clean the instance, we pass "" as the 'process' since this command isn't really
        # specific to any process and needs to be just run on the node.
        self.cloud.run_control_script("", "clean-instance", args, self.extra_vars, host_info)


class OnPremPrecheckInstanceMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        self.current_ssh_user = None
        super(OnPremPrecheckInstanceMethod, self).__init__(base_command, "precheck")

    def preprocess_args(self, args):
        super(OnPremPrecheckInstanceMethod, self).preprocess_args(args)
        if args.precheck_type == "configure":
            self.current_ssh_user = "yugabyte"

    def wait_for_host(self, args, default_port=True):
        logging.info("Waiting for instance {}".format(args.search_pattern))
        host_info = self.cloud.get_host_info(args)
        if host_info:
            self.extra_vars.update(
                get_ssh_host_port(host_info, args.custom_ssh_port))
            # Expect onprem nodes to already exist.
            if wait_for_ssh(self.extra_vars["ssh_host"],
                            self.extra_vars["ssh_port"],
                            self.extra_vars["ssh_user"],
                            args.private_key_file,
                            num_retries=SSH_RETRY_LIMIT_PRECHECK, ssh2_enabled=args.ssh2_enabled):
                return host_info
        else:
            raise YBOpsRuntimeError("Unable to find host info.")

    def get_ssh_user(self):
        return self.current_ssh_user

    def add_extra_args(self):
        super(OnPremPrecheckInstanceMethod, self).add_extra_args()
        self.parser.add_argument('--master_http_port', default=DEFAULT_MASTER_HTTP_PORT)
        self.parser.add_argument('--master_rpc_port', default=DEFAULT_MASTER_RPC_PORT)
        self.parser.add_argument('--tserver_http_port', default=DEFAULT_TSERVER_HTTP_PORT)
        self.parser.add_argument('--tserver_rpc_port', default=DEFAULT_TSERVER_RPC_PORT)
        self.parser.add_argument('--cql_proxy_http_port', default=None)
        self.parser.add_argument('--cql_proxy_rpc_port', default=None)
        self.parser.add_argument('--ysql_proxy_http_port', default=None)
        self.parser.add_argument('--ysql_proxy_rpc_port', default=None)
        self.parser.add_argument('--redis_proxy_http_port', default=None)
        self.parser.add_argument('--redis_proxy_rpc_port', default=None)
        self.parser.add_argument('--node_exporter_http_port', default=None)
        self.parser.add_argument('--root_cert_path', default=None)
        self.parser.add_argument('--server_cert_path', default=None)
        self.parser.add_argument('--server_key_path', default=None)
        self.parser.add_argument('--root_cert_path_client_to_server', default=None)
        self.parser.add_argument('--server_cert_path_client_to_server', default=None)
        self.parser.add_argument('--server_key_path_client_to_server', default=None)
        self.parser.add_argument('--certs_location_client_to_server', default=None)
        self.parser.add_argument('--client_cert_path', default=None)
        self.parser.add_argument('--client_key_path', default=None)
        self.parser.add_argument('--skip_cert_validation', default=None)

        self.parser.add_argument("--precheck_type", required=True,
                                 choices=['provision', 'configure'],
                                 help="Preflight check to determine if instance is ready.")
        self.parser.add_argument("--air_gap", action="store_true",
                                 help='If instances are air gapped or not.')
        self.parser.add_argument("--install_node_exporter", action="store_true",
                                 help='Check if node exporter can be installed properly.')
        self.parser.add_argument("--skip_ntp_check", action="store_true",
                                 help='Skip check for time synchronization.')

    def verify_certificates(self, cert_type, root_cert_path, cert_path, key_path, ssh_options,
                            skip_cert_validation, results):
        result_var = True
        remote_shell = RemoteShell(ssh_options)
        if not self.test_file_readable(remote_shell, root_cert_path):
            results["File {} is present and readable".format(root_cert_path)] = False
            result_var = False
        if not self.test_file_readable(remote_shell, cert_path):
            results["File {} is present and readable".format(cert_path)] = False
            result_var = False
        if not self.test_file_readable(remote_shell, key_path):
            results["File {} is present and readable".format(key_path)] = False
            result_var = False
        if result_var and skip_cert_validation != 'ALL':
            try:
                self.cloud.verify_certs(root_cert_path, cert_path, ssh_options,
                                        skip_cert_validation != 'HOSTNAME')
            except YBOpsRuntimeError as e:
                result_var = False
                results["Check {} certificate".format(cert_type)] = str(e)

        if result_var:
            results["Check {} certificate".format(cert_type)] = True
        return result_var

    def test_file_readable(self, remote_shell, path):
        node_file_verify = remote_shell.run_command_raw("test -r {}".format(path))
        return node_file_verify.exited == 0

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance: {} does not exist, cannot run preflight checks"
                                    .format(args.search_pattern))

        results = {}
        logging.info("Running {} preflight checks for instance: {}".format(
            args.precheck_type, args.search_pattern))

        self.update_ansible_vars_with_args(args)
        self.update_ansible_vars_with_host_info(host_info, args.custom_ssh_port)
        try:
            is_configure = args.precheck_type == "configure"
            self.wait_for_host(args, default_port=is_configure)
        except YBOpsRuntimeError as e:
            logging.info("Failed to connect to node {}: {}".format(args.search_pattern, e))
            # No point continuing test if ssh fails.
            results["SSH Connection"] = False
            print(json.dumps(results, indent=2))
            return

        scp_result = scp_to_tmp(
            get_datafile_path('preflight_checks.sh'), self.extra_vars["private_ip"],
            self.extra_vars["ssh_user"], self.extra_vars["ssh_port"], args.private_key_file,
            ssh2_enabled=args.ssh2_enabled)

        results["SSH Connection"] = scp_result == 0

        ssh_options = {
            "ssh_user": "yugabyte",
            "ssh_host": self.extra_vars["private_ip"],
            "ssh_port": self.extra_vars["ssh_port"],
            "private_key_file": args.private_key_file,
            "ssh2_enabled": args.ssh2_enabled
        }

        if args.root_cert_path is not None:
            self.verify_certificates("Server", args.root_cert_path,
                                     args.server_cert_path,
                                     args.server_key_path,
                                     ssh_options,
                                     args.skip_cert_validation,
                                     results)

        if args.root_cert_path_client_to_server is not None:
            self.verify_certificates("Server clientRootCA", args.root_cert_path_client_to_server,
                                     args.server_cert_path_client_to_server,
                                     args.server_key_path_client_to_server,
                                     ssh_options,
                                     args.skip_cert_validation,
                                     results)

        if args.client_cert_path is not None:
            root_cert_path = args.root_cert_path_client_to_server \
                if args.root_cert_path_client_to_server is not None else args.root_cert_path
            self.verify_certificates("Client", root_cert_path,
                                     args.client_cert_path,
                                     args.client_key_path,
                                     ssh_options,
                                     'HOSTNAME',  # not checking hostname for that serts
                                     results)

        sudo_pass_file = '/tmp/.yb_sudo_pass.sh'
        self.extra_vars['sudo_pass_file'] = sudo_pass_file
        ansible_status = self.cloud.setup_ansible(args).run("send_sudo_pass.yml",
                                                            self.extra_vars, host_info,
                                                            print_output=False)
        results["Try Ansible Command"] = ansible_status == 0

        ports_to_check = ",".join([str(p) for p in [args.master_http_port,
                                                    args.master_rpc_port,
                                                    args.tserver_http_port,
                                                    args.tserver_rpc_port,
                                                    args.cql_proxy_http_port,
                                                    args.cql_proxy_rpc_port,
                                                    args.ysql_proxy_http_port,
                                                    args.ysql_proxy_rpc_port,
                                                    args.redis_proxy_http_port,
                                                    args.redis_proxy_rpc_port,
                                                    args.node_exporter_http_port] if p is not None])

        cmd = "/tmp/preflight_checks.sh --type {} --yb_home_dir {} --mount_points {} " \
              "--ports_to_check {} --sudo_pass_file {} --cleanup".format(
                args.precheck_type, YB_HOME_DIR, self.cloud.get_mount_points_csv(args),
                ports_to_check, sudo_pass_file)
        if args.install_node_exporter:
            cmd += " --install_node_exporter"
        if args.air_gap:
            cmd += " --airgap"

        self.update_ansible_vars_with_args(args)
        self.update_ansible_vars_with_host_info(host_info, args.custom_ssh_port)
        rc, stdout, stderr = remote_exec_command(
            self.extra_vars["private_ip"], self.extra_vars["ssh_port"],
            self.extra_vars["ssh_user"], args.private_key_file, cmd,
            ssh2_enabled=args.ssh2_enabled)

        if rc != 0:
            results["Preflight Script Error"] = stderr
        else:
            # stdout will be returned as a list of lines, which should just be one line of json.
            if isinstance(stdout, list):
                stdout = stdout[0]
            stdout = json.loads(stdout)
            stdout = {k: v == "true" for k, v in iteritems(stdout)}
            results.update(stdout)

        output = json.dumps(results, indent=2)
        print(output)


class OnPremFillInstanceProvisionTemplateMethod(AbstractMethod):
    def __init__(self, base_command):
        super(OnPremFillInstanceProvisionTemplateMethod, self).__init__(base_command, 'template')

    def add_extra_args(self):
        super(OnPremFillInstanceProvisionTemplateMethod, self).add_extra_args()
        self.parser.add_argument('--name', default='provision_instance.py', required=False,
                                 help='Desired name for the new provision instance script')
        self.parser.add_argument('--destination', required=True,
                                 help='Directory to put the new provision instance script in.')
        self.parser.add_argument('--ssh_user', required=True,
                                 help='The user who can SSH into the instance.')
        # Allow for ssh_port to not be mandatory so already created template scripts just default
        # to 22, without breaking...
        self.parser.add_argument('--custom_ssh_port', required=False, default=22,
                                 help='The port on which to SSH into the instance.')
        self.parser.add_argument('--local_package_path', required=True,
                                 help='Path to the local third party dependency packages.')
        self.parser.add_argument('--private_key_file', required=True,
                                 help='Private key file to ssh into the instance.')
        self.parser.add_argument('--passwordless_sudo', action='store_true',
                                 help='If the ssh_user has passwordless sudo access or not.')
        self.parser.add_argument("--air_gap", action="store_true",
                                 help='If instances are air gapped or not.')
        self.parser.add_argument("--node_exporter_port", type=int,
                                 default=DEFAULT_NODE_EXPORTER_HTTP_PORT,
                                 help="The port for node_exporter to bind to")
        self.parser.add_argument("--node_exporter_user", default="prometheus")
        self.parser.add_argument("--install_node_exporter", action="store_true")
        self.parser.add_argument("--use_chrony", action="store_true",
                                 help="Whether to set up chrony for NTP synchronization.")
        self.parser.add_argument("--ntp_server", required=False, action="append", default=[],
                                 help="NTP server to connect to.")

    def callback(self, args):
        config = {'devops_home': ybutils.YB_DEVOPS_HOME_PERM, 'cloud': self.cloud.name}
        file_name = 'provision_instance.py.j2'
        try:
            config.update(vars(args))
            config["yb_home_dir"] = YB_HOME_DIR
            data_dir = os.path.dirname(get_datafile_path(file_name))
            template = Environment(loader=FileSystemLoader(data_dir)).get_template(file_name)
            with open(os.path.join(args.destination, args.name), 'w') as f:
                f.write(template.render(config))
            os.chmod(f.name, stat.S_IRWXU)
            print(json.dumps({'script_path': f.name}))
        except Exception as e:
            logging.error(e)
            print(json.dumps(
                {"error": "Unable to create script: {}".format(get_exception_message(e))}))


class OnPremAccessAddKeyMethod(AbstractAccessMethod):
    def __init__(self, base_command):
        super(OnPremAccessAddKeyMethod, self).__init__(base_command, "add-key")

    def callback(self, args):
        (private_key_file, public_key_file) = self.validate_key_files(args)
        print(json.dumps({"private_key": private_key_file, "public_key": public_key_file}))
