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
from ybops.utils.remote_shell import copy_to_tmp, wait_for_server, RemoteShell
from ybops.node_agent.server_pb2 import PreflightCheckInput
from ybops.utils.ssh import SSH_RETRY_LIMIT_PRECHECK

import json
import logging
import os
import stat
import ybops.utils as ybutils
import re
import ssl
from six import iteritems
from http.client import HTTPConnection, HTTPSConnection
from urllib.parse import urlparse


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
            self.get_server_host_port({"private_ip": args.search_pattern}, args.custom_ssh_port))
        connect_options = {}
        connect_options.update(self.extra_vars)

        print(validate_instance(self.extra_vars,
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
        self.update_ansible_vars_with_args(args)
        if args.mount_points:
            for host_info in host_infos:
                try:
                    connect_options = {}
                    connect_options.update(self.extra_vars)
                    connect_options.update({
                        "ssh_user": host_info['ssh_user'],
                        "node_agent_user": host_info['ssh_user']
                    })
                    connect_options.update(self.get_server_host_port(
                                        self.cloud.get_host_info(args),
                                        args.custom_ssh_port))
                    host_info['mount_roots'] = get_mount_roots(connect_options, args.mount_points)

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
        self.parser.add_argument("--clean_node_exporter", action="store_true",
                                 help='Check if node exporter should be stopped and disabled.')
        self.parser.add_argument("--provisioning_cleanup", action="store_true",
                                 help='Check if provisioned services cleanup should be skipped.')
        self.parser.add_argument("--clean_otel_collector", action="store_true",
                                 help='Check if OTel Collector should be stopped and disabled.')

    def update_ansible_vars_with_args(self, args):
        super(OnPremDestroyInstancesMethod, self).update_ansible_vars_with_args(args)
        if args.clean_node_exporter:
            self.extra_vars["clean_node_exporter"] = args.clean_node_exporter
        if args.clean_otel_collector:
            self.extra_vars["clean_otel_collector"] = args.clean_otel_collector

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            logging.error("Host {} does not exists.".format(args.search_pattern))
            return
        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port))

        # Force db-related commands to use the "yugabyte" user.
        ssh_user = args.ssh_user
        args.ssh_user = "yugabyte"
        self.update_ansible_vars_with_args(args)

        # First stop both tserver and master processes.
        processes = ["tserver", "master", "controller"]
        if args.clean_otel_collector and args.provisioning_cleanup:
            processes.append("otel-collector")

        for process in processes:
            logging.info(("[app] Running control script to stop {} at {}")
                         .format(process, host_info['name']))
            self.cloud.run_control_script(process, "stop-destroy", args,
                                          self.extra_vars, host_info)

        # Revert the force using of user yugabyte.
        args.ssh_user = ssh_user
        self.update_ansible_vars_with_args(args)

        if args.provisioning_cleanup:
            self.cloud.run_control_script(
                "platform-services", "remove-services", args, self.extra_vars, host_info)

        # Run non-db related tasks.
        if args.clean_node_exporter and args.provisioning_cleanup:
            logging.info(("[app] Running control script remove-services " +
                          "against thirdparty services at {}").format(host_info['name']))
            self.cloud.run_control_script(
                "thirdparty", "remove-services", args, self.extra_vars, host_info)

        # Use "yugabyte" user again to do the cleanups.
        args.ssh_user = "yugabyte"
        self.update_ansible_vars_with_args(args)
        logging.info(("[app] Running control script to clean and clean-logs " +
                     "against master, tserver and controller at {}").format(host_info['name']))
        for process in processes:
            for command in ["clean", "clean-logs"]:
                self.cloud.run_control_script(
                    process, command, args, self.extra_vars, host_info)

        # Now clean the instance, we pass "" as the 'process' since this command isn't really
        # specific to any process and needs to be just run on the node.
        logging.info(("[app] Running control script to clean-instance " +
                      "against node {}").format(host_info['name']))
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
                self.get_server_host_port(host_info, args.custom_ssh_port))
            # Expect onprem nodes to already exist.
            if wait_for_server(self.extra_vars, num_retries=SSH_RETRY_LIMIT_PRECHECK):
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
        self.parser.add_argument('--yb_controller_http_port', default=None)
        self.parser.add_argument('--yb_controller_rpc_port', default=None)
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

        if self.get_connection_type() == "ssh":
            scp_result = copy_to_tmp(self.extra_vars, get_datafile_path('preflight_checks.sh'),
                                     remote_tmp_dir=args.remote_tmp_dir)
            results["SSH Connection"] = scp_result == 0

        sudo_pass_file = '{}/.yb_sudo_pass.sh'.format(args.remote_tmp_dir)
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

        if self.get_connection_type() == "ssh":
            cmd = "{}/preflight_checks.sh --type {} --yb_home_dir {} --mount_points {} " \
                "--ports_to_check {} --sudo_pass_file {} --tmp_dir {} --cleanup".format(
                    args.remote_tmp_dir, args.precheck_type, YB_HOME_DIR,
                    self.cloud.get_mount_points_csv(args),
                    ports_to_check, sudo_pass_file, args.remote_tmp_dir)
            if args.install_node_exporter:
                cmd += " --install_node_exporter"
            if args.air_gap:
                cmd += " --airgap"

            rc, stdout, stderr = remote_exec_command(self.extra_vars, cmd)

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
        else:
            input = PreflightCheckInput()
            input.ybHomeDir = YB_HOME_DIR
            input.airGapInstall = args.air_gap
            input.installNodeExporter = args.install_node_exporter
            input.mountPaths.extend(self.cloud.get_mount_points_csv(args).split(","))
            input.skipProvisioning = True if args.precheck_type == 'configure' else False
            if args.master_http_port:
                input.masterHttpPort = int(args.master_http_port)
            if args.master_rpc_port:
                input.masterRpcPort = int(args.master_rpc_port)
            if args.tserver_http_port:
                input.tserverHttpPort = int(args.tserver_http_port)
            if args.tserver_rpc_port:
                input.tserverRpcPort = int(args.tserver_rpc_port)
            if args.redis_proxy_http_port:
                input.redisServerHttpPort = int(args.redis_proxy_http_port)
            if args.redis_proxy_rpc_port:
                input.redisServerRpcPort = int(args.redis_proxy_rpc_port)
            if args.node_exporter_http_port:
                input.nodeExporterPort = int(args.node_exporter_http_port)
            if args.cql_proxy_http_port:
                input.ycqlServerHttpPort = int(args.cql_proxy_http_port)
            if args.cql_proxy_rpc_port:
                input.ycqlServerRpcPort = int(args.cql_proxy_rpc_port)
            if args.ysql_proxy_http_port:
                input.ysqlServerHttpPort = int(args.ysql_proxy_http_port)
            if args.ysql_proxy_rpc_port:
                input.ysqlServerRpcPort = int(args.ysql_proxy_rpc_port)
            if args.yb_controller_http_port:
                input.ybControllerHttpPort = int(args.yb_controller_http_port)
            if args.yb_controller_rpc_port:
                input.ybControllerRpcPort = int(args.yb_controller_rpc_port)
            remote_shell = RemoteShell(self.extra_vars)
            json_response = remote_shell.invoke_method(input)
            print(json_response)


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
        self.parser.add_argument("--provider_id", required=True,
                                 help="Provider ID.")
        self.parser.add_argument("--install_node_agent", action="store_true", default=False,
                                 help="Install node agent in provisioning.")
        self.parser.add_argument("--node_agent_port", default=9070, required=False,
                                 help="Node agent server port.")

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


class OnPremInstallNodeAgentMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(OnPremInstallNodeAgentMethod, self).__init__(base_command, 'install-node-agent')

    def preprocess_args(self, args):
        super(OnPremInstallNodeAgentMethod, self).preprocess_args(args)

    def add_extra_args(self):
        super(OnPremInstallNodeAgentMethod, self).add_extra_args()
        self.parser.add_argument("--yba_url", help="Base YBA URL reachable from DB node.",
                                 required=True)
        self.parser.add_argument("--api_token", help="API token for YBA.", required=True)
        self.parser.add_argument("--node_name", help="Node name.", required=True)
        self.parser.add_argument("--provider_id", help="Provider ID or name.", required=True)
        self.parser.add_argument("--region_name", help="Region name.", required=True)
        self.parser.add_argument("--zone_name", help="Zone name.", required=True)

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance: {} does not exist, cannot install node agent"
                                    .format(args.search_pattern))
        self.update_ansible_vars_with_args(args)
        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port))
        url = urlparse(args.yba_url)
        self.https = True if url.scheme == 'https' else False
        self.yba_host = url.hostname
        self.yba_port = url.port
        self.auth_header = {"X-AUTH-YW-API-TOKEN": args.api_token}
        self.install_user = "yugabyte"
        self.local_installer_path = os.path.join(args.tmp_dir, "node-agent-installer.sh")
        self.remote_installer_path = os.path.join(args.remote_tmp_dir, "node-agent-installer.sh")
        self.sudo_pass_file = '{}/.yb_sudo_pass.sh'.format(args.remote_tmp_dir)
        self.extra_vars['sudo_pass_file'] = self.sudo_pass_file
        self.cloud.setup_ansible(args).run("send_sudo_pass.yml",
                                           self.extra_vars, host_info,
                                           print_output=False)
        remote_shell = RemoteShell(self.extra_vars)
        try:
            self.copy_installer_script(args)
            self.uninstall_node_agent(args)
            self.install_node_agent(args)
        finally:
            remote_shell.exec_command(f'rm -rf "{self.sudo_pass_file}"'
                                      f' "{self.remote_installer_path}"')
            remote_shell.close()

    def get_http_connection(self, args):
        return HTTPSConnection(host=self.yba_host,
                               port=self.yba_port,
                               context=ssl._create_unverified_context(),
                               timeout=10) if self.https else HTTPConnection(host=self.yba_host,
                                                                             port=self.yba_port,
                                                                             timeout=10)

    def get_node_os_arch(self, args):
        cmd = ["uname", "-sm"]
        remote_shell = RemoteShell(self.extra_vars)
        try:
            result = remote_shell.run_command(cmd)
            parts = re.split("\\s+", result.stdout)
            return parts[0], parts[1]
        finally:
            remote_shell.close()

    def copy_installer_script(self, args):
        os_type, arch_type = self.get_node_os_arch(args)
        conn = self.get_http_connection(args)
        try:
            # Download the installer locally.
            conn.request("GET", f"/api/v1/node_agents/download?os={os_type}&arch={arch_type}",
                         headers=self.auth_header)
            with open(self.local_installer_path, "wb") as file:
                response = conn.getresponse()
                if response.status != 200:
                    raise YBOpsRuntimeError("Failed({}) to download node-agent installer."
                                            .format(response.status))
                file.write(response.read())
            st = os.stat(self.local_installer_path)
            os.chmod(self.local_installer_path,
                     st.st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
            # Copy over the installer to the remote host.
            # File permissions are preserved.
            copy_to_tmp(self.extra_vars, self.local_installer_path,
                        remote_tmp_dir=args.remote_tmp_dir)
        finally:
            conn.close()

    def uninstall_node_agent(self, args):
        remote_shell = RemoteShell(self.extra_vars)
        try:
            sudo_pass = os.getenv("YB_SUDO_PASS", None)
            uninstall_cmd_args = f"cd {args.remote_tmp_dir} &&"
            if sudo_pass:
                uninstall_cmd_args += f" . {self.sudo_pass_file}"
                uninstall_cmd_args += " && echo -n $YB_SUDO_PASS | sudo -H -S"
            else:
                uninstall_cmd_args += " sudo -H -n"
            uninstall_cmd_args += f" {self.remote_installer_path} -c uninstall -u {args.yba_url}"
            uninstall_cmd_args += f" -t {args.api_token} --node_ip {args.node_agent_ip}"
            uninstall_cmd_args += " --skip_verify_cert"
            uninstall_cmd = ["bash", "-c", uninstall_cmd_args]
            remote_shell.run_command(uninstall_cmd)
        finally:
            remote_shell.close()

    def install_node_agent(self, args):
        remote_shell = RemoteShell(self.extra_vars)
        try:
            sudo_pass = os.getenv("YB_SUDO_PASS", None)
            install_cmd_args = f"cd {args.remote_tmp_dir} &&"
            if sudo_pass:
                install_cmd_args += f". {self.sudo_pass_file}"
                install_cmd_args += " && echo -n $YB_SUDO_PASS | sudo -H -S"
            else:
                install_cmd_args += " sudo -H -n"
            install_cmd_args += f" -u {self.install_user}"
            install_cmd_args += f" {self.remote_installer_path} -c install -u {args.yba_url}"
            install_cmd_args += f" -t {args.api_token} --skip_verify_cert --silent"
            install_cmd_args += f" --user {self.install_user}"
            install_cmd_args += f" --node_name {args.node_name} --node_ip {args.node_agent_ip}"
            install_cmd_args += f" --node_port {args.node_agent_port}"
            install_cmd_args += f" --provider_id {args.provider_id}"
            install_cmd_args += f" --instance_type {args.instance_type}"
            install_cmd_args += f" --region_name {args.region_name}"
            install_cmd_args += f" --zone_name {args.zone_name}"
            install_cmd = ["bash", "-c", install_cmd_args]
            # Run the installer script.
            remote_shell.run_command(install_cmd)
            # Install service as root.
            service_cmd_args = f"cd {args.remote_tmp_dir} &&"
            if sudo_pass:
                service_cmd_args += f" . {self.sudo_pass_file}"
                service_cmd_args += " && echo -n $YB_SUDO_PASS | sudo -H -S"
            else:
                service_cmd_args += " sudo -H -n"
            service_cmd_args += f" {self.remote_installer_path}"
            service_cmd_args += f" -c install_service --user {self.install_user}"
            service_cmd = ["bash", "-c", service_cmd_args]
            remote_shell.run_command(service_cmd)
        finally:
            remote_shell.close()


class OnPremVerifyCertificatesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(OnPremVerifyCertificatesMethod, self).__init__(base_command, "verify_certs")

    def add_extra_args(self):
        super(OnPremVerifyCertificatesMethod, self).add_extra_args()
        # NodeToNode certificates
        self.parser.add_argument("--root_cert_path", type=str, required=False, default=None)
        self.parser.add_argument("--yba_root_cert_checksum", type=str, required=False, default=None)
        self.parser.add_argument("--node_server_cert_path", type=str, required=False, default=None)
        self.parser.add_argument("--node_server_key_path", type=str, required=False, default=None)
        # ClientToNode certificates
        # client_rootCA and yba_client_root_cert_checksum are not required if
        # they are same as rootCA
        self.parser.add_argument("--client_root_cert_path", type=str, required=False,
                                 default=None)
        self.parser.add_argument("--yba_client_root_cert_checksum", type=str, required=False,
                                 default=None)
        self.parser.add_argument("--client_server_cert_path", type=str, required=False,
                                 default=None)
        self.parser.add_argument("--client_server_key_path", type=str, required=False, default=None)

        self.parser.add_argument("--skip_hostname_check", action="store_true")

    def verify_custom_certificate_checksum(self, root_cert_path, yba_cert_checksum,
                                           connect_options):
        remote_shell = RemoteShell(connect_options)
        check_md5_present = remote_shell.run_command_raw("which md5sum")
        if check_md5_present.exited != 0:
            logging.warning("md5sum command not found on the node. Skipping checksum verification.")
            return
        root_ca_checksum_response = remote_shell.run_command_raw("md5sum {}".format(root_cert_path))
        if root_ca_checksum_response.exited != 0:
            raise YBOpsRuntimeError("Failed to get checksum for root CA certificate")
        root_ca_checksum = root_ca_checksum_response.stdout.split()[0]
        if root_ca_checksum != yba_cert_checksum:
            raise YBOpsRuntimeError(
                "Root Certificate on the node doesn't match the certificate given to YBA.")

    def verify_certificates(self, cert_type, root_cert_path, yba_cert_checksum, cert_path,
                            key_path, connect_options, verify_hostname, results):
        """
            This validation is only used for onprem + customCertHostPath certs.
            For more generic validation, use the verify_certs method in cloud.py
            and in CertificateHelper.java class.
        """
        result_var = True
        try:
            # Do all the basic checks here
            self.cloud.verify_certs(root_cert_path, cert_path, key_path, connect_options,
                                    verify_hostname, perform_extended_validation=True)
            # Do the CA checksum check here since its unique to onprem + customCertHostPath certs
            self.verify_custom_certificate_checksum(root_cert_path, yba_cert_checksum,
                                                    connect_options)
        except YBOpsRuntimeError as e:
            result_var = False
            results["{} certificate".format(cert_type)] = str(e)
        if result_var:
            results["{} certificate".format(cert_type)] = True

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Instance: {} does not exist, cannot verify certificates"
                                    .format(args.search_pattern))
        self.update_ansible_vars_with_args(args)
        self.extra_vars.update(self.get_server_host_port(host_info, args.custom_ssh_port))
        results = {}
        # Verify NodeToNode certificates
        if args.root_cert_path is not None:
            self.verify_certificates("RootCA", args.root_cert_path,
                                     args.yba_root_cert_checksum,
                                     args.node_server_cert_path,
                                     args.node_server_key_path,
                                     self.extra_vars,
                                     not args.skip_hostname_check,
                                     results)

        # Verify ClientToNode certificates
        if args.client_root_cert_path is not None:
            self.verify_certificates("ClientRootCA", args.client_root_cert_path,
                                     args.yba_client_root_cert_checksum,
                                     args.client_server_cert_path,
                                     args.client_server_key_path,
                                     self.extra_vars,
                                     not args.skip_hostname_check,
                                     results)

        for key, value in results.items():
            logging.debug("Certificate verification result: {}: {}".format(key, value))
            if value is not True:
                logging.error(json.dumps(results, indent=2))
                raise YBOpsRuntimeError(
                    "Certificate verification for {} on node - {} failed with error: {}".format(
                        key, args.search_pattern, value))
