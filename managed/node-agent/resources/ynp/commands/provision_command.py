import importlib
import os
import tempfile
import subprocess
import logging
import pkgutil
import jinja2
import sys

import modules.base_module as mbm
from .base_command import Command

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
            temp_file.write("set -ex\n")
            self.populate_sudo_check(temp_file)
            for key in all_templates:
                temp_file.write(f"\n######## BEGIN {key} #########\n")
                temp_file.write(all_templates[key][phase])
                temp_file.write(f"\n######## END {key} #########\n")
        os.chmod(temp_file.name, 0o755)
        logger.info(temp_file.name)
        return temp_file.name

    def _run_script(self, script_path):
        result = subprocess.run(["/bin/bash", "-lc", script_path], capture_output=True, text=True)
        logger.info("Output: %s", result.stdout)
        logger.info("Error: %s", result.stderr)
        logger.info("Return Code: %s", result.returncode)

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
            logger.info(context)
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

    def run_preflight_checks(self):
        _, precheck_combined_script = self._generate_template()
        self._run_script(precheck_combined_script)

    def cleanup(self):
        # Cleanup tasks to clean up any tmp data to support rerun
        pass

    def done(self):
        pass
