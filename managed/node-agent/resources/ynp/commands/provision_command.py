import importlib
import os
import tempfile
import subprocess
import logging
import pkgutil
import jinja2

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
        # perform basic validation code
        pass

    def _load_modules(self):
        package = importlib.import_module(self.base_package)
        package_path = package.__path__[0]

        for root, _, _ in os.walk(package_path):
            relative_path = os.path.relpath(root, package_path)
            module_base = self.base_package if relative_path == "." else f"{self.base_package}.{relative_path.replace(os.sep, '.')}"

            for loader, module_name, is_pkg in pkgutil.iter_modules([root]):
                full_module_name = f"{module_base}.{module_name}"
                importlib.import_module(full_module_name)

    def _build_script(self, all_templates, phase):
        with tempfile.NamedTemporaryFile(mode="w+", delete=False) as temp_file:
            temp_file.write("#!/bin/bash\n\n")
            temp_file.write("set -ex")
            for key in all_templates:
                temp_file.write(f"\n######## {key} #########\n")
                temp_file.write(all_templates[key][phase])
                temp_file.write(f"\n######## {key} #########\n")
        os.chmod(temp_file.name, 0o755)
        logger.info(temp_file.name)
        return temp_file.name

    def _run_script(self, script_path):
        result = subprocess.run(["/bin/bash", "-lc", script_path], capture_output=True, text=True)
        logger.info("Output: %s", result.stdout)
        logger.info("Error: %s", result.stderr)
        logger.info("Return Code: %s", result.returncode)

    def execute(self):
        all_templates = {}

        for key in self.config:
            module = self._module_registry.get(key)
            if module is None:
                continue
            context = self.config[key]

            context["templatedir"] = os.path.join(os.path.dirname(module[1]), "templates")
            logger.info(context)
            module_instance = module[0]()
            all_templates[key] = module_instance.render_templates(context)

        precheck_combined_script = self._build_script(all_templates, "precheck")
        run_combined_script = self._build_script(all_templates, "run")

        self._run_script(run_combined_script)
        self._run_script(precheck_combined_script)

    def run_preflight_checks(self):
        pass

    def cleanup(self):
        # Cleanup tasks to clean up any tmp data to support rerun
        pass

    def done(self):
        pass
