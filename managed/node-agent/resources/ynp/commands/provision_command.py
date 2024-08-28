import importlib
import os
import tempfile
import subprocess
import logging
import pkgutil
import sys
import pwd
import grp
import semver
import stat

import modules.base_module as mbm
from .base_command import Command
from utils.util import safely_write_file

logger = logging.getLogger(__name__)


class ProvisionCommand(Command):

    def __init__(self, config):
        super().__init__(config)
        self.base_package = "modules"
        self._module_registry = mbm.BaseYnpModule.registry
        self._load_modules()
        logger.info(self._module_registry)
        logger.info("initialized")

    def validate(self):
        # Validate the required packages needed for the provision to be successful.
        self._validate_required_packages()
        self._validate_permissions()

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

    def _build_script(self, all_templates, phase):
        with tempfile.NamedTemporaryFile(mode="w+", delete=False) as temp_file:
            temp_file.write("#!/bin/bash\n\n")
            temp_file.write("set -e\n")
            self.add_results_helper(temp_file)
            self.populate_sudo_check(temp_file)
            for key in all_templates:
                temp_file.write(f"\n######## BEGIN {key} #########\n")
                temp_file.write(all_templates[key][phase])
                temp_file.write(f"\n######## END {key} #########\n")
            self.print_results_helper(temp_file)

        os.chmod(temp_file.name, 0o755)
        logger.info(temp_file.name)
        return temp_file.name

    def _run_script(self, script_path):
        result = subprocess.run(["/bin/bash", "-lc", script_path], capture_output=True, text=True)
        logger.info("Output: %s", result.stdout)
        logger.info("Error: %s", result.stderr)
        logger.info("Return Code: %s", result.returncode)

    def add_results_helper(self, file):
        file.write("""
            # Initialize the JSON results array
            json_results='{"results":['

            add_result() {
                local check="$1"
                local result="$2"
                local message="$3"
                if [ "${#json_results}" -gt 12 ]; then
                    json_results+=','
                fi
                json_results+='{"check":"'$check'","result":"'$result'","message":"'$message'"}'
            }
        """)

    def print_results_helper(self, file):
        file.write("""
            print_results() {
                any_fail=0
                if [[ $json_results == *'"result":"FAIL"'* ]]; then
                    any_fail=1
                fi
                json_results+=']}'

                # Output the JSON
                echo "$json_results"

                # Exit with status code 1 if any check has failed
                if [ $any_fail -eq 1 ]; then
                    echo "Pre-flight checks failed, Please fix them before continuing."
                    exit 1
                else
                    echo "Pre-flight checks successful"
                fi
            }

            print_results
        """)

    def populate_sudo_check(self, file):
        file.write("\n######## Check the SUDO Access #########\n")
        file.write("SUDO_ACCESS=\"false\"\n")
        file.write("set +e\n")
        file.write("if [ $(id -u) = 0 ]; then\n")
        file.write("  SUDO_ACCESS=\"true\"\n")
        file.write("elif sudo -n pwd >/dev/null 2>&1; then\n")
        file.write("  SUDO_ACCESS=\"true\"\n")
        file.write("fi\n")
        file.write("set -e\n")

    def _generate_template(self):
        all_templates = {}

        for key in self.config:
            module = self._module_registry.get(key)
            if module is None:
                continue
            context = self.config[key]

            context["templatedir"] = os.path.join(os.path.dirname(module[1]), "templates")
            module_instance = module[0]()
            rendered_template = module_instance.render_templates(context)
            if rendered_template is not None:
                all_templates[key] = rendered_template

        precheck_combined_script = self._build_script(all_templates, "precheck")
        run_combined_script = self._build_script(all_templates, "run")

        return run_combined_script, precheck_combined_script

    def _check_package(self, package_manager, package_name):
        """Check if a package is installed."""
        try:
            if package_manager == 'rpm':
                subprocess.run(['rpm', '-q', package_name], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            elif package_manager == 'deb':
                subprocess.run(['dpkg', '-s', package_name], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            logger.info(f"{package_name} is installed.")
        except subprocess.CalledProcessError:
            logger.info(f"{package_name} is not installed.")
            sys.exit()

    def _validate_permissions(self):
        gp_dir = os.path.dirname(os.path.dirname(self.config["ynp_dir"]))
        installer_dir = os.path.join(gp_dir, "bin")
        mode = os.stat(installer_dir).st_mode
        yugabyte_has_read = bool(mode & stat.S_IROTH)
        yugabyte_has_execute = bool(mode & stat.S_IXOTH)
        if yugabyte_has_read and yugabyte_has_execute:
            logger.info(f"yugabyte user has read and execute permissions on {installer_dir}")
        else:
            logger.error(f"yugabyte does NOT have sufficient permissions on {installer_dir}")
            logger.error(f"Please fix the permissions on {installer_dir} and try again.")
            sys.exit(1)

    def _validate_required_packages(self):
        package_manager = None
        try:
            subprocess.run(['rpm', '--version'], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            package_manager = 'rpm'
        except FileNotFoundError:
            try:
                subprocess.run(['dpkg', '--version'], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                package_manager = 'deb'
            except FileNotFoundError:
                logger.info(
                    "Unsupported package manager. Cannot determine package installation status.")
                sys.exit(1)

        if package_manager is None:
            logger.info(
                "Unsupported package manager. Cannot determine package installation status.")
            sys.exit(1)

        packages = ['openssl', 'policycoreutils']
        for package in packages:
            self._check_package(package_manager, package)

    def execute(self):
        run_combined_script, precheck_combined_script = self._generate_template()
        self._run_script(run_combined_script)
        self._run_script(precheck_combined_script)
        self._save_ynp_version()

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
                yb_user = context.get('yb_user')
                uid = pwd.getpwnam(yb_user).pw_uid
                gid = grp.getgrnam(yb_user).gr_gid
                os.chown(ynp_version_file, uid, gid)
            else:
                logger.info("yb_home_dir or current_ynp_version is missing in the context")

    def _compare_ynp_version(self):
        key = next(iter(self.config), None)
        if key is not None:
            context = self.config[key]
            yb_home_dir = context.get('yb_home_dir')
            current_ynp_version = semver.Version.parse(context.get('version'))

            # Define the full path to the ynp_version file
            ynp_version_file = os.path.join(yb_home_dir, 'ynp_version')
            try:
                # Read the ynp_version from the file
                with open(ynp_version_file, 'r') as file:
                    stored_ynp_version = semver.Version.parse(file.read().strip())
            except FileNotFoundError:
                logger.error(f"The ynp_version file was not found at {ynp_version_file}")
                sys.exit(1)
            except ValueError as e:
                logger.error(f"Error parsing version from the ynp_version file: {e}")
                sys.exit(1)

            if current_ynp_version.major != stored_ynp_version.major:
                logger.info(
                    f"The major versions are different. Current: {current_ynp_version},"
                    f"Stored: {stored_ynp_version}. "
                    "Please run reprovision again on the node"
                    )
                sys.exit(1)

    def run_preflight_checks(self):
        _, precheck_combined_script = self._generate_template()
        self._compare_ynp_version()
        self._run_script(precheck_combined_script)

    def cleanup(self):
        # Cleanup tasks to clean up any tmp data to support rerun
        pass

    def done(self):
        pass
