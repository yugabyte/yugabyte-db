#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from ybops.cloud.common.ansible import AnsibleProcess
from ybops.cloud.common.base import AbstractCommandParser
from ybops.utils import get_ssh_host_port, get_datafile_path, \
    get_internal_datafile_path, YBOpsRuntimeError, YB_HOME_DIR

from ybops.utils.remote_shell import RemoteShell

from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import (load_pem_private_key, Encoding,
                                                          PrivateFormat, NoEncryption)
import datetime
import logging
import os
import re
import six
import ssl
import tempfile
import yaml
import ipaddress


class AbstractCloud(AbstractCommandParser):
    """Class that encapsulates the first layer of abstraction of commands, at the cloud level.

    This should be responsible for keeping high level data and options, as well as holding
    instances to cloud-specific structures or APIs. This class is also responsible for providing
    ways of calling out to Ansible.
    """
    VARS_DIR_SUFFIX = "vars/cloud"
    BASE_IMAGE_VERSION_KEY = "base_image_version"
    KEY_SIZE = 2048
    PUBLIC_EXPONENT = 65537
    CERT_VALID_DURATION = 365
    YSQLSH_CERT_DIR = os.path.join(YB_HOME_DIR, ".yugabytedb")
    ROOT_CERT_NAME = "ca.crt"
    ROOT_CERT_NEW_NAME = "ca_new.crt"
    CLIENT_ROOT_NAME = "root.crt"
    CLIENT_CERT_NAME = "yugabytedb.crt"
    CLIENT_KEY_NAME = "yugabytedb.key"

    def __init__(self, name):
        super(AbstractCloud, self).__init__(name)
        devops_home = os.environ.get("yb_devops_home")
        vars_file = os.path.join(devops_home,
                                 AbstractCloud.VARS_DIR_SUFFIX,
                                 "{}.yml".format(self.name))
        self.ansible_vars = yaml.load(open(vars_file), yaml.SafeLoader)
        with open(vars_file, 'r') as f:
            self.ansible_vars = yaml.load(f, yaml.SafeLoader) or {}

        # The metadata file name is the same internally and externally.
        metadata_filename = "{}-metadata.yml".format(self.name)
        self.metadata = {}
        # Fetch the dicts and update in order.
        # Default dict is the public metadata file.
        # Afterwards, if available, we update it with the internal version.
        for path_getter in [get_datafile_path, get_internal_datafile_path]:
            path = path_getter(metadata_filename)
            if os.path.isfile(path):
                with open(path) as ymlfile:
                    metadata = yaml.load(ymlfile, yaml.SafeLoader)
                    self.metadata.update(metadata)

    def update_metadata(self, override_filename):
        metadata_override = {}
        with open(override_filename) as yml_file:
            metadata_override = yaml.load(yml_file)
        for key in ["regions"]:
            value = metadata_override.get(key)
            if value:
                self.metadata[key] = value

    def validate_credentials(self):
        potential_env_vars = self.metadata.get('credential_vars')
        missing_var = None
        if potential_env_vars:
            for var in potential_env_vars:
                if var not in os.environ:
                    missing_var = var
                    break
        # If we found cloud credentials, then we're good to go and will explicitly use those!
        if missing_var is None:
            logging.info("Found {} cloud credentials in env.".format(self.name))
            return
        # If no cloud credentials, see if we have credentials on the machine itself.
        if self.has_machine_credentials():
            logging.info("Found {} cloud credentials in machine metadata.".format(self.name))
            return
        raise YBOpsRuntimeError(
            "Cloud {} missing {} and has no machine credentials to default to.".format(
                self.name, missing_var))

    def has_machine_credentials(self):
        return False

    def init_cloud_api(self, args=None):
        """Override to lazily initialize cloud-specific APIs and clients.
        """
        pass

    def network_bootstrap(self, args):
        """Override to do custom network bootstrap code for the respective cloud.
        """
        pass

    def network_cleanup(self, args):
        """Override to do custom network cleanup code for the respective cloud.
        """
        pass

    def get_default_base_image_version(self):
        return self.ansible_vars.get(self.BASE_IMAGE_VERSION_KEY)

    def get_image_by_version(self, region, version=None):
        """Override to get image using cloud-specific APIs and clients.
        """
        pass

    def setup_ansible(self, args):
        """Prepare and return a base AnsibleProcess class as well as setup some initial arguments,
        such as the cloud_type, for the respective playbooks.
        Args:
            args: the parsed command-line arguments, as setup by the relevant ArgParse instance.
        """
        ansible = AnsibleProcess()
        ansible.playbook_args["cloud_type"] = self.name
        if args.region:
            ansible.playbook_args["cloud_region"] = args.region
        if args.zone:
            ansible.playbook_args["cloud_zone"] = args.zone
        if hasattr(args, "custom_ssh_port") and args.custom_ssh_port:
            ansible.playbook_args["custom_ssh_port"] = args.custom_ssh_port
        return ansible

    def add_extra_args(self):
        """Override to setup cloud-specific command line flags.
        """
        self.parser.add_argument("--region", required=False)
        self.parser.add_argument("--zone", required=False)
        self.parser.add_argument("--network", required=False)

    def add_subcommand(self, command):
        """Subclass override to set a reference to the cloud into the subcommands we add.
        """
        command.cloud = self
        super(AbstractCloud, self).add_subcommand(command)

    def run_control_script(self, process, command, args, extra_vars, host_info):
        updated_vars = {
            "process": process,
            "command": command
        }
        updated_vars.update(extra_vars)
        updated_vars.update(get_ssh_host_port(host_info, args.custom_ssh_port))
        if os.environ.get("YB_USE_FABRIC", False):
            remote_shell = RemoteShell(updated_vars)
            file_path = os.path.join(YB_HOME_DIR, "bin/yb-server-ctl.sh")
            remote_shell.run_command(
                "{} {} {}".format(file_path, process, command)
            )
        else:
            self.setup_ansible(args).run("yb-server-ctl.yml", updated_vars, host_info)

    def initYSQL(self, master_addresses, ssh_options):
        remote_shell = RemoteShell(ssh_options)
        init_db_path = os.path.join(YB_HOME_DIR, "tserver/postgres/bin/initdb")
        remote_shell.run_command(
            "bash -c \"YB_ENABLED_IN_POSTGRES=1 FLAGS_pggate_master_addresses={} "
            "{} -D /tmp/yb_pg_initdb_tmp_data_dir "
            "-U postgres\"".format(master_addresses, init_db_path)
        )

    def compare_root_certs(self, extra_vars, ssh_options):
        has_openssl = True
        root_cert_path = extra_vars["root_cert_path"]
        certs_node_dir = extra_vars["certs_node_dir"]
        yb_root_cert_path = os.path.join(certs_node_dir, self.ROOT_CERT_NAME)
        remote_shell = RemoteShell(ssh_options)
        try:
            remote_shell.run_command('which openssl')
        except YBOpsRuntimeError as e:
            # No openssl, just compare files.
            has_openssl = False

        # Check if files exist. If not, let it error out (since the root files should be there.)
        remote_shell.run_command('test -f {}'.format(root_cert_path))
        remote_shell.run_command('test -f {}'.format(yb_root_cert_path))

        # Compare the openssl hash if openssl is present.
        if has_openssl:
            md5_cmd = "openssl x509 -noout -modulus -in '{}' | openssl md5"
            curr_root = remote_shell.run_command(md5_cmd.format(yb_root_cert_path))
            new_root = remote_shell.run_command(md5_cmd.format(root_cert_path))
            if curr_root.stdout != new_root.stdout:
                raise YBOpsRuntimeError("Root certs are different.")
        # Openssl not present, just compare the files after removing whitespace.
        else:
            # If there is an error code, that means the files are different. Should fail.
            remote_shell.run_command('diff -Z {} {}'.format(root_cert_path, yb_root_cert_path))

    # Compare certificate content and return
    # 0 -> if cert1 equals to cert2
    # 1 -> if cert1 is subset of cert2
    # 2 -> if cert2 is subset of cert1
    # 3 -> if none of the above satisfies
    def compare_certs(self, cert1, cert2):
        # Extract certificate values list from the string
        cert1_re = re.findall(
            "-----BEGIN.*?-----([\\s\\S]*?)-----END.*?-----", cert1, re.M)
        cert2_re = re.findall(
            "-----BEGIN.*?-----([\\s\\S]*?)-----END.*?-----", cert2, re.M)
        # Trim spaces and newlines and form a set
        cert1_set = \
            {"".join(y.strip() for y in x.splitlines()) for x in cert1_re}
        cert2_set = \
            {"".join(y.strip() for y in x.splitlines()) for x in cert2_re}

        if cert1_set == cert2_set:
            return 0
        if cert1_set < cert2_set:
            return 1
        if cert1_set > cert2_set:
            return 2

        return 3

    def append_new_root_cert(self, extra_vars, ssh_options, is_custom_cert=False):
        remote_shell = RemoteShell(ssh_options)
        certs_dir = extra_vars["certs_node_dir"]
        yb_root_cert_path = os.path.join(certs_dir, self.ROOT_CERT_NAME)
        yb_root_cert_new_path = os.path.join(certs_dir, self.ROOT_CERT_NEW_NAME)

        # Give write permissions to cert directory
        remote_shell.run_command('chmod -f 666 {}/* || true'.format(certs_dir))

        # Copy the new root cert to ca_new.crt
        if is_custom_cert:
            root_cert_path = extra_vars["root_cert_path"]
            remote_shell.run_command("cp '{}' '{}'".format(root_cert_path,
                                                           yb_root_cert_new_path))
        else:
            root_cert_path = extra_vars["rootCA_cert"]
            remote_shell.put_file(root_cert_path, yb_root_cert_new_path)

        # Append new cert content to ca.crt
        remote_shell.run_command(
            "cat '{}' >> '{}'".format(yb_root_cert_new_path, yb_root_cert_path))
        # Reset the write permissions
        remote_shell.run_command('chmod 400 {}/*'.format(certs_dir))

    def remove_old_root_cert(self, extra_vars, ssh_options):
        remote_shell = RemoteShell(ssh_options)
        certs_dir = extra_vars["certs_node_dir"]
        yb_root_cert_path = os.path.join(certs_dir, self.ROOT_CERT_NAME)
        yb_root_cert_new_path = os.path.join(certs_dir, self.ROOT_CERT_NEW_NAME)
        # Check if ca_new.crt is present, it will be present if
        # APPEND_NEW_ROOT_CERT action was performed before
        file_verify = remote_shell.run_command_raw(
            "test -f '{}'".format(yb_root_cert_new_path))
        # No action needed if ca_new.crt is not present
        if not file_verify.exited:
            # Give write permissions to cert directory
            remote_shell.run_command(
                'chmod -f 666 {}/* || true'.format(certs_dir))
            # Remove ca.crt and rename ca_new.crt to ca.crt
            remote_shell.run_command("mv '{}' '{}'".format(
                yb_root_cert_new_path, yb_root_cert_path))
            # Reset the write permissions
            remote_shell.run_command('chmod 400 {}/*'.format(certs_dir))

    def __verify_certs_hostname(self, node_crt_path, ssh_options):
        host = ssh_options["ssh_host"]
        remote_shell = RemoteShell(ssh_options)
        logging.info("Verifying Subject for certs {}".format(node_crt_path))

        # Get readable text version of cert
        cert_text = remote_shell.run_command(
            "openssl x509 -noout -text -in {}".format(node_crt_path))
        if "Certificate:" not in cert_text.stdout:
            raise YBOpsRuntimeError("Unable to decode the node cert: {}.".format(node_crt_path))

        # Extract commonName and subjectAltName from the cert text output
        regex_out = re.findall(" Subject:.*CN=([\\S]*)$| (DNS|IP Address):([\\S]*?)(,|$)",
                               cert_text.stdout, re.M)
        # Hostname will be present in group 0 for CN and in group 1 and 2 for SAN
        cn_entry = [x[0] for x in regex_out if x[0] != '']
        san_entry = {(x[1], x[2]) for x in regex_out if x[0] == ''}

        # Create cert object following the below dictionary format
        # https://docs.python.org/3/library/ssl.html#ssl.SSLSocket.getpeercert
        cert_cn = {'subject': ((('commonName', cn_entry[0] if len(cn_entry) > 0 else ''),),)}
        cert_san = {'subjectAltName': tuple(san_entry)}

        # Check if the provided hostname matches with either CN or SAN
        cn_matched = False
        san_matched = False
        try:
            ssl.match_hostname(cert_cn, host)
            cn_matched = True
        except ssl.CertificateError:
            pass
        try:
            ssl.match_hostname(cert_san, host)
            san_matched = True
        except ssl.CertificateError:
            pass

        if not cn_matched and not san_matched:
            raise YBOpsRuntimeError(
                "'{}' does not match with any entry in CN or SAN of the node cert: {}, "
                "cert_cn: {}, cert_san: {}".format(host, node_crt_path, cert_cn, cert_san))

    def verify_certs(self, root_crt_path, node_crt_path, ssh_options, verify_hostname=False):
        remote_shell = RemoteShell(ssh_options)

        try:
            remote_shell.run_command('which openssl')
        except YBOpsRuntimeError:
            logging.debug("Openssl not found, skipping certificate verification.")
            return

        # Verify if the node cert is not expired
        validity_verify = remote_shell.run_command_raw(
            "openssl x509 -noout -checkend 86400 -in {}".format(node_crt_path))
        if validity_verify.exited == 1:
            raise YBOpsRuntimeError(
                "Node cert: {} is expired or will expire in one day.".format(node_crt_path))

        # Verify if node cert has valid signature
        signature_verify = remote_shell.run_command_raw(
            "openssl verify -CAfile {} {} | egrep error".format(root_crt_path, node_crt_path))
        if signature_verify.exited != 1:
            raise YBOpsRuntimeError(
                "Node cert: {} is not signed by the provided root cert: {}.".format(node_crt_path,
                                                                                    root_crt_path))

        if verify_hostname:
            self.__verify_certs_hostname(node_crt_path, ssh_options)

    def copy_certs(self, extra_vars, ssh_options, rotate_certs=False,
                   skip_cert_hostname_validation=False):
        remote_shell = RemoteShell(ssh_options)
        node_ip = ssh_options["ssh_host"]
        root_cert_path = extra_vars["root_cert_path"]
        node_cert_path = extra_vars["node_cert_path"]
        node_key_path = extra_vars["node_key_path"]
        certs_dir = extra_vars["certs_node_dir"]
        key_file = 'node.{}.key'.format(node_ip)
        cert_file = 'node.{}.crt'.format(node_ip)
        yb_root_cert_path = os.path.join(certs_dir, self.ROOT_CERT_NAME)
        yb_server_cert_path = os.path.join(certs_dir, cert_file)
        yb_server_key_path = os.path.join(certs_dir, key_file)

        verify_hostname = True
        if skip_cert_hostname_validation:
            logging.info("Skipping host name validation for certs")
            verify_hostname = False

        self.verify_certs(root_cert_path, node_cert_path, ssh_options, verify_hostname)

        copy_root = True
        if rotate_certs:
            root_cert = remote_shell.run_command(
                "cat '{}'".format(yb_root_cert_path)).stdout
            root_cert_new = remote_shell.run_command(
                "cat '{}'".format(root_cert_path)).stdout
            if root_cert is not None and root_cert_new is not None:
                compare_result = self.compare_certs(root_cert_new, root_cert)
                if compare_result == 0 or compare_result == 1:
                    # Don't copy root certs if the new root cert is
                    # same or subset of the existing root cert
                    copy_root = False
            else:
                raise YBOpsRuntimeError(
                    "Unable to fetch the certificate {}".format(root_cert_path))

        logging.info("Moving certs located at {}, {}, {}.".format(root_cert_path, node_cert_path,
                                                                  node_key_path))

        remote_shell.run_command('mkdir -p ' + certs_dir)
        # Give write permission in case file exists. If the command fails, ignore.
        remote_shell.run_command('chmod -f 666 {}/* || true'.format(certs_dir))
        if copy_root:
            remote_shell.run_command("cp '{}' '{}'".format(root_cert_path,
                                                           yb_root_cert_path))
        remote_shell.run_command("cp '{}' '{}'".format(node_cert_path,
                                                       yb_server_cert_path))
        remote_shell.run_command("cp '{}' '{}'".format(node_key_path,
                                                       yb_server_key_path))
        remote_shell.run_command('chmod 400 {}/*'.format(certs_dir))
        if "client_cert_path" in extra_vars:
            client_cert_path = extra_vars["client_cert_path"]
            client_key_path = extra_vars["client_key_path"]
            logging.info("Moving client certs located at {}, {}.".format(client_cert_path,
                                                                         client_key_path))
            remote_shell.run_command('mkdir -p ' + self.YSQLSH_CERT_DIR)
            # Give write permission in case file exists. If the command fails, ignore.
            remote_shell.run_command('chmod -f 666 {}/* || true'.format(self.YSQLSH_CERT_DIR))
            remote_shell.run_command("cp '{}' '{}'".format(client_cert_path,
                                                           os.path.join(self.YSQLSH_CERT_DIR,
                                                                        self.CLIENT_CERT_NAME)))
            remote_shell.run_command("cp '{}' '{}'".format(client_key_path,
                                                           os.path.join(self.YSQLSH_CERT_DIR,
                                                                        self.CLIENT_KEY_NAME)))
            remote_shell.run_command('chmod 400 {}/*'.format(self.YSQLSH_CERT_DIR))

    def generate_client_cert(self, extra_vars, ssh_options, rotate_certs=False):
        node_ip = ssh_options["ssh_host"]
        root_cert_path = extra_vars["rootCA_cert"]
        root_key_path = extra_vars["rootCA_key"]
        certs_node_dir = extra_vars["certs_node_dir"]
        yb_root_cert_path = os.path.join(certs_node_dir, self.ROOT_CERT_NAME)

        with open(root_cert_path, 'rb') as cert_in:
            certlines = cert_in.read()
        with open(root_key_path, 'rb') as key_in:
            keylines = key_in.read()

        copy_root = True
        remote_shell = RemoteShell(ssh_options)
        if rotate_certs:
            root_cert = remote_shell.run_command(
                "cat '{}'".format(yb_root_cert_path)).stdout
            root_cert_new = str(certlines)
            if root_cert is not None and root_cert_new is not None:
                compare_result = self.compare_certs(root_cert_new, root_cert)
                if compare_result == 0 or compare_result == 1:
                    # Don't copy root certs if the new root cert is
                    # same or subset of the existing root cert
                    copy_root = False
            else:
                raise YBOpsRuntimeError(
                    "Unable to fetch the certificate {}".format(root_cert_path))

        root_cert = x509.load_pem_x509_certificate(certlines, default_backend())
        root_key = load_pem_private_key(keylines, None, default_backend())
        private_key = rsa.generate_private_key(
            public_exponent=self.PUBLIC_EXPONENT,
            key_size=self.KEY_SIZE,
            backend=default_backend()
        )
        public_key = private_key.public_key()
        builder = x509.CertificateBuilder()
        builder = builder.subject_name(x509.Name([
            x509.NameAttribute(NameOID.COMMON_NAME, six.text_type(node_ip)),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, six.text_type(extra_vars["org_name"]))
        ]))
        builder = builder.issuer_name(root_cert.subject)
        builder = builder.not_valid_before(datetime.datetime.utcnow())
        builder = builder.not_valid_after(datetime.datetime.utcnow() + datetime.timedelta(
            extra_vars["cert_valid_duration"]))
        builder = builder.serial_number(x509.random_serial_number())
        builder = builder.public_key(public_key)
        builder = builder.add_extension(x509.BasicConstraints(ca=False, path_length=None),
                                        critical=True)
        builder = builder.add_extension(x509.SubjectAlternativeName(
                                        [x509.IPAddress(ipaddress.ip_address(node_ip))]),
                                        critical=False)
        certificate = builder.sign(private_key=root_key, algorithm=hashes.SHA256(),
                                   backend=default_backend())
        # Write private key to file
        pem = private_key.private_bytes(
            encoding=Encoding.PEM,
            format=PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=NoEncryption()
        )
        key_file = 'node.{}.key'.format(node_ip)
        cert_file = 'node.{}.crt'.format(node_ip)
        with tempfile.TemporaryDirectory() as common_path:
            with open(os.path.join(common_path, key_file), 'wb') as pem_out:
                pem_out.write(pem)
            # Write certificate to file
            pem = certificate.public_bytes(encoding=Encoding.PEM)
            with open(os.path.join(common_path, cert_file), 'wb') as pem_out:
                pem_out.write(pem)
            # Copy files over to node
            remote_shell.run_command('mkdir -p ' + certs_node_dir)
            # Give write permission in case file exists. If the command fails, ignore.
            remote_shell.run_command('chmod -f 666 {}/* || true'.format(certs_node_dir))
            remote_shell.put_file(os.path.join(common_path, key_file),
                                  os.path.join(certs_node_dir, key_file))
            remote_shell.put_file(os.path.join(common_path, cert_file),
                                  os.path.join(certs_node_dir, cert_file))
            if copy_root:
                remote_shell.put_file(root_cert_path, yb_root_cert_path)
            remote_shell.run_command('chmod 400 {}/*'.format(certs_node_dir))

            if "client_cert" in extra_vars:
                client_cert_path = extra_vars["client_cert"]
                client_key_path = extra_vars["client_key"]
                remote_shell.run_command('mkdir -p ' + self.YSQLSH_CERT_DIR)
                # Give write permission in case file exists. If the command fails, ignore.
                remote_shell.run_command('chmod -f 666 {}/* || true'.format(self.YSQLSH_CERT_DIR))
                remote_shell.put_file(root_cert_path, os.path.join(self.YSQLSH_CERT_DIR,
                                                                   self.CLIENT_ROOT_NAME))
                remote_shell.put_file(client_cert_path, os.path.join(self.YSQLSH_CERT_DIR,
                                                                     self.CLIENT_CERT_NAME))
                remote_shell.put_file(client_key_path, os.path.join(self.YSQLSH_CERT_DIR,
                                                                    self.CLIENT_KEY_NAME))
                remote_shell.run_command('chmod 400 {}/*'.format(self.YSQLSH_CERT_DIR))

    def create_encryption_at_rest_file(self, extra_vars, ssh_options):
        encryption_key_path = extra_vars["encryption_key_file"]  # Source file path
        key_node_dir = extra_vars["encryption_key_dir"]  # Target file path
        with open(encryption_key_path, "r") as f:
            encryption_key = f.read()
        key_file = os.path.basename(encryption_key_path)
        with tempfile.TemporaryDirectory() as common_path:
            # Write encryption-at-rest key to file
            with open(os.path.join(common_path, key_file), 'wb') as key_out:
                key_out.write(encryption_key)
            # Copy files over to node
            remote_shell = RemoteShell(ssh_options)
            remote_shell.run_command('mkdir -p ' + key_node_dir)
            remote_shell.put_file(os.path.join(common_path, key_file),
                                  os.path.join(key_node_dir, key_file))

    def get_host_info(self, args, get_all=False):
        """Use this to override in subclasses to use cloud-specific APIs to search the cloud
        instances for something that matches the given arguments.

        This method only returns one single entry that matches the search pattern, to be used
        internally by all code paths that require data about a certain instance.

        Args:
            args: the parsed command-line arguments, as setup by the relevant ArgParse instance.
        """
        return None

    def get_device_names(self, args):
        return []

    def get_mount_points_csv(self, args):
        if args.mount_points:
            return args.mount_points
        else:
            return ",".join(["/mnt/d{}".format(i) for i in range(args.num_volumes)])

    def expand_file_system(self, args, ssh_options):
        remote_shell = RemoteShell(ssh_options)
        mount_points = self.get_mount_points_csv(args).split(',')
        for mount_point in mount_points:
            logging.info("Expanding file system with mount point: {}".format(mount_point))
            remote_shell.run_command('sudo xfs_growfs {}'.format(mount_point))
