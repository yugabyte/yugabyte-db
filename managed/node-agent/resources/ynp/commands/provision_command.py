import importlib
import os
import tempfile
import subprocess
import logging
import pkgutil
import sys
import pwd
import grp
import shutil

from enum import Enum
import modules.base_module as mbm
from .base_command import Command
from utils.util import safely_write_file

logger = logging.getLogger(__name__)


class OSFamily(Enum):
    REDHAT = "RedHat"
    DEBIAN = "Debian"
    SUSE = "Suse"
    ARCH = "Arch"
    UNKNOWN = "Unknown"


class ProvisionCommand(Command):

    cloud_only_modules = ['Preprovision', 'MountEphemeralDrive', 'InstallPackages']
    onprem_only_modules = ['RebootNode']
    required_os_packages = {
        OSFamily.REDHAT.value: ['openssl', 'policycoreutils'],
        OSFamily.DEBIAN.value: ['openssl', 'policycoreutils'],
        OSFamily.SUSE.value: ['openssl'],
        OSFamily.ARCH.value: ['openssl', 'policycoreutils'],
    }

    def __init__(self, config):
        super().__init__(config)
        self.base_package = "modules"
        self._module_registry = mbm.BaseYnpModule.registry
        self._discover_os_info()
        self._discover_package_manager()
        self._load_modules()
        logger.info(self._module_registry)
        logger.info("initialized")

    def validate(self):
        # Validate the required packages needed for the provision to be successful.
        self._validate_required_packages()

    def _load_modules(self):
        package = importlib.import_module(self.base_package)
        package_path = package.__path__[0]

        for root, _, _ in os.walk(package_path):
            relative_path = os.path.relpath(root, package_path)
            module_base = (self.base_package if relative_path == "."
                           else f"{self.base_package}.{relative_path.replace(os.sep, '.')}")

            for loader, module_name, is_pkg in pkgutil.iter_modules([root]):
                full_module_name = f"{module_base}.{module_name}"
                importlib.import_module(full_module_name)

    def _build_script(self, all_templates, phase, create_subshell=False):
        key = next(iter(self.config), None)
        context = self.config[key]
        with tempfile.NamedTemporaryFile(mode="w+", dir=context.get('tmp_directory'),
                                         delete=False) as temp_file:
            temp_file.write("#!/bin/bash\n\n")
            loglevel = context.get('loglevel')
            if loglevel == "DEBUG":
                temp_file.write("set -x\n")
            if create_subshell:
                # Initialize parent exit code and errors array.
                temp_file.write("parent_exit_code=0\n")
                temp_file.write("errors=()\n")
                # Add fatal error helpers for run phase (modules may call add_fatal_result)
                self.add_fatal_helpers_for_run(temp_file)
            else:
                # add_result_helper works only in the same shell.
                self.add_results_helper(temp_file)
            self.populate_sudo_check(temp_file)
            for key in all_templates:
                temp_file.write(f"\n######## BEGIN {key} #########\n")
                template = all_templates[key][phase]
                if template is not None and template.strip():
                    if create_subshell:
                        temp_file.write("(\n")
                        temp_file.write(f"echo \"Executing module {key}\"\n")
                        temp_file.write(template)
                        temp_file.write("\n)\n")
                        self.add_exit_code_check(temp_file, key)
                    else:
                        temp_file.write(f"echo \"Executing module {key}\"\n")
                        temp_file.write(template)
                temp_file.write(f"\n######## END {key} #########\n")
            if create_subshell:
                temp_file.write("\n######## Summary #########\n")
                self.print_exit_errors(temp_file)
            else:
                self.print_results_helper(temp_file)

        os.chmod(temp_file.name, 0o755)
        logger.info("Temp file for " + phase + " is: " + temp_file.name)
        return temp_file.name

    def _run_script(self, script_path):
        result = subprocess.run(["/bin/bash", "-lc", script_path], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, universal_newlines=True)
        logger.info("Output: %s", result.stdout)
        logger.info("Error: %s", result.stderr)
        logger.info("Return Code: %s", result.returncode)
        return result

    def add_fatal_helpers_for_run(self, file):
        """Add add_fatal_result for run phase (no JSON)."""
        file.write(
            """
            add_fatal_result() {
                local check="$1"
                local message="$2"
                echo "FATAL ERROR: $check - $message"
                exit 2
            }
            """
        )

    def add_exit_code_check(self, file, key):
        file.write(
            f"""
            exit_code=$?
            if [ $exit_code -ne 0 ]; then
                parent_exit_code=$exit_code
                err="Module {key} failed with code $exit_code"
                errors+=("$err")
                echo "$err"
                if [ $exit_code -eq 2 ]; then
                    echo "FATAL error in module {key}. Aborting immediately."
                    exit 2
                fi
            fi
            """
        )

    def print_exit_errors(self, file):
        file.write(
            """
            if [ ${#errors[@]} -ne 0 ]; then
                for err in "${errors[@]}"; do
                    echo "$err"
                done
            fi
            exit $parent_exit_code
            """
        )

    def add_results_helper(self, file):
        file.write(
            """
            # Initialize the JSON results array
            json_results='{\n"results":[\n'

            add_result() {
                local check="$1"
                local result="$2"
                local message="$3"
                if [ "${#json_results}" -gt 20 ]; then
                    json_results+=',\n'
                fi
                json_results+='    {\n'
                json_results+='      "check": "'$check'",\n'
                json_results+='      "result": "'$result'",\n'
                json_results+='      "message": "'$message'"\n'
                json_results+='    }'
            }

            add_fatal_result() {
                local check="$1"
                local message="$2"
                echo "FATAL ERROR: $check - $message"
                add_result "$check" "FATAL" "$message"
                json_results+='\n]}'
                echo "$json_results"
                exit 2
            }
            """
        )

    def print_results_helper(self, file):
        file.write("""
            print_results() {
                any_fail=0
                if [[ $json_results == *'"result": "FAIL"'* ]]; then
                    any_fail=1
                fi
                json_results+='\n]}'

                # Output the JSON
                echo "$json_results"

                if [ $any_fail -eq 1 ]; then
                    echo "Pre-flight checks failed, Please fix them before continuing."
                    exit 1
                fi
                echo "Pre-flight checks successful"
            }

            print_results
        """)

    def populate_sudo_check(self, file):
        file.write("\n######## Check the SUDO Access #########\n")
        file.write("SUDO_ACCESS=\"false\"\n")
        file.write("if [ $(id -u) = 0 ]; then\n")
        file.write("  SUDO_ACCESS=\"true\"\n")
        file.write("elif sudo -n pwd >/dev/null 2>&1; then\n")
        file.write("  SUDO_ACCESS=\"true\"\n")
        file.write("fi\n")

    def _generate_template(self, specific_module=None):
        all_templates = {}

        for key in self.config:
            module = self._module_registry.get(key)
            if module is None:
                continue
            if specific_module is not None and key != specific_module:
                print(f"Skipping {key} because requested specific module {specific_module}")
                continue
            if key in self.cloud_only_modules and \
                    self.config[key].get('is_cloud', 'False') == 'False':
                print(f"Skipping {key} because is_cloud is {self.config[key].get('is_cloud')}")
                continue
            if key in self.onprem_only_modules and \
                    self.config[key].get('is_cloud', 'False') == 'True':
                print(f"Skipping {key} because is_cloud is {self.config[key].get('is_cloud')}")
                continue
            if key == 'InstallNodeAgent' and \
                    self.config[key].get('is_install_node_agent', 'True') == 'False':
                print(f"Skipping {key} because is_install_node_agent is "
                      f"{self.config[key].get('is_install_node_agent')}")
                continue
            if key == 'ConfigureClockbound' and \
                    self.config[key].get('configure_clockbound', 'False') == 'False':
                print(f"Skipping {key} because {key}.configure_clockbound is "
                      f"{self.config[key].get('configure_clockbound')}")
                continue
            if key == 'ConfigureSudoers' and \
                    not self.config[key].get('sudoers_commands', None):
                print(f"Skipping {key} because {key}.sudoers_commands is not set")
                continue
            context = self.config[key]

            context["templatedir"] = os.path.join(os.path.dirname(module[1]), "templates")
            context["os_family"] = self.os_family
            context["os_version"] = self.os_version
            context["os_distribution"] = self.os_distribution
            module_instance = module[0]()
            rendered_template = module_instance.render_templates(context)
            if rendered_template is not None:
                all_templates[key] = rendered_template

        precheck_combined_script = self._build_script(all_templates, "precheck")
        run_combined_script = self._build_script(all_templates, "run", create_subshell=True)

        return run_combined_script, precheck_combined_script

    def _discover_os_info(self):
        os_release_file = '/etc/os-release'

        # Check if the os-release file exists
        if not os.path.isfile(os_release_file):
            return None, None, None

        os_release_info = {}

        # Parse the os-release file
        with open(os_release_file) as f:
            for line in f:
                if '=' in line:
                    key, value = line.strip().split('=', 1)
                    os_release_info[key] = value.strip('"')

        # Extract distribution and version
        self.os_distribution = os_release_info.get("ID", "").lower()
        version = os_release_info.get("VERSION_ID", "")
        self.os_version = version.split('.')[0] if version else ""
        # Determine OS family
        self.os_family = OSFamily.UNKNOWN.value
        if self.os_distribution in {"rhel", "centos", "almalinux", "ol", "fedora"}:
            self.os_family = OSFamily.REDHAT.value
        elif self.os_distribution in {"ubuntu", "debian"}:
            self.os_family = OSFamily.DEBIAN.value
        elif self.os_distribution in {"suse", "opensuse", "sles"}:
            self.os_family = OSFamily.SUSE.value
        elif self.os_distribution == "arch":
            self.os_family = OSFamily.ARCH.value
        logger.info(f"Detected OS Distribution: {self.os_distribution}, "
                    f"Version: {self.os_version}, Family: {self.os_family}")

    def _check_package(self, package_name):
        """Check if a package is installed."""
        try:
            if self.package_manager == 'rpm':
                subprocess.run(['rpm', '-q', package_name], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            elif self.package_manager == 'deb':
                subprocess.run(['dpkg', '-s', package_name], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            logger.info(f"{package_name} is installed.")
        except subprocess.CalledProcessError:
            logger.info(f"{package_name} is not installed.")
            sys.exit(1)

    def _discover_package_manager(self):
        package_manager = None
        try:
            subprocess.run(['rpm', '--version'], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.package_manager = 'rpm'
        except FileNotFoundError:
            try:
                subprocess.run(['dpkg', '--version'], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.package_manager = 'deb'
            except FileNotFoundError:
                logger.info(
                    "Unsupported package manager. Cannot determine package installation status.")
                sys.exit(1)

        if self.package_manager is None:
            logger.info(
                "Unsupported package manager. Cannot determine package installation status.")
            sys.exit(1)

        logger.info(f"Detected package manager: {self.package_manager}")

    def _validate_required_packages(self):
        packages = self.required_os_packages.get(self.os_family, [])
        for package in packages:
            self._check_package(package)
        key = next(iter(self.config), None)
        context = self.config[key]
        is_cloud = context.get('is_cloud', 'False')
        if is_cloud == 'True':
            cloud_only_packages = ['gzip']
            for package in cloud_only_packages:
                self._check_package(package)

    def _copy_templates_files_for_ybm(self, context):
        ynp_dir = context.get('ynp_dir')
        modules_path = os.path.join(ynp_dir, 'modules/provision')
        systemd_dir_path = os.path.join(modules_path, 'systemd/templates/')
        ybm_dir_path = os.path.join(modules_path, 'ybm_ami/templates/')
        files = ['clean_cores.sh.j2', 'zip_purge_yb_logs.sh.j2', 'collect_metrics_wrapper.sh.j2']
        for f in files:
            src = os.path.join(systemd_dir_path, f)
            dest = os.path.join(ybm_dir_path, f)
            shutil.copy(src, dest)

    def execute(self, specific_module=None):
        key = next(iter(self.config), None)
        if key is not None:
            context = self.config[key]
            if context.get('is_ybm'):
                self._copy_templates_files_for_ybm(context)
        run_combined_script, precheck_combined_script = self._generate_template(specific_module)
        provision_result = self._run_script(run_combined_script)
        precheck_result = self._run_script(precheck_combined_script)
        self._save_ynp_version()
        if precheck_result.returncode != 0 or provision_result.returncode != 0:
            exit_code = (provision_result.returncode
                         if provision_result.returncode != 0
                         else precheck_result.returncode)
            sys.exit(exit_code)

    def _save_ynp_version(self):
        key = next(iter(self.config), None)
        if key is not None:
            context = self.config[key]
            current_ynp_version = context.get('version')
            yb_home_dir = context.get('yb_home_dir')

            # Ensure yb_home_dir exists
            if yb_home_dir and current_ynp_version:
                os.makedirs(yb_home_dir, exist_ok=True)

                # Define the full path to the ynp_version file
                ynp_version_file = os.path.join(yb_home_dir, 'ynp_version')
                safely_write_file(ynp_version_file, current_ynp_version)
                current_user_id = os.getuid()
                yb_user = context.get('yb_user')
                uid = pwd.getpwnam(yb_user).pw_uid
                gid = grp.getgrnam(yb_user).gr_gid
                if current_user_id == 0 and current_user_id != uid:
                    # Change ownership only if running as root and yb_user is different
                    # from current user.
                    os.chown(ynp_version_file, uid, gid)
            else:
                logger.info("yb_home_dir or current_ynp_version is missing in the context")

    def _compare_ynp_version(self):
        key = next(iter(self.config), None)
        if key is not None:
            context = self.config[key]
            yb_home_dir = context.get('yb_home_dir')
            current_ynp_version = self._parse_version(context.get('version'))

            # Define the full path to the ynp_version file
            ynp_version_file = os.path.join(yb_home_dir, 'ynp_version')
            try:
                # Read the ynp_version from the file
                with open(ynp_version_file, 'r') as file:
                    stored_ynp_version = self._parse_version(file.read().strip())
            except FileNotFoundError:
                logger.error(f"The ynp_version file was not found at {ynp_version_file}")
                sys.exit(1)
            except ValueError as e:
                logger.error(f"Error parsing version from the ynp_version file: {e}")
                sys.exit(1)

            if current_ynp_version[0] != stored_ynp_version[0]:
                logger.info(
                    f"The major versions are different. Current: {current_ynp_version},"
                    f"Stored: {stored_ynp_version}. "
                    "Please run reprovision again on the node"
                )
                sys.exit(1)

    def _parse_version(self, version):
        """
        Parse a version string into a tuple of integers (major, minor, patch).
        :param version: str, the version string (e.g., '1.2.3').
        :return: tuple, (major, minor, patch).
        """
        try:
            parts = version.split('.')
            if len(parts) != 3:
                raise ValueError(f"Invalid version format: {version}")
            return tuple(int(part) for part in parts)
        except (ValueError, AttributeError):
            raise ValueError(f"Invalid version format: {version}")

    def run_preflight_checks(self):
        _, precheck_combined_script = self._generate_template()
        self._compare_ynp_version()
        self._run_script(precheck_combined_script)

    def dry_run(self):
        install_script, precheck_script = self._generate_template()
        logger.info("Install Script: %s", install_script)
        logger.info("Precheck Script: %s", precheck_script)

    def cleanup(self):
        # Cleanup tasks to clean up any tmp data to support rerun
        pass

    def done(self):
        pass
