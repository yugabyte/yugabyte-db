import modules.base_module as mbm
import importlib
import jinja2
import os
import logging
import tempfile
import pkgutil
import subprocess

# Get logger for this module
logger = logging.getLogger(__name__)


class YugabyteUniverseNodeProvisioner:
    def __init__(self):
        self.config = None
        self._modules = None
        self.base_package = "modules"
        self._module_registry = mbm.BaseYnpModule.registry
        return

    def validate(self):
        # perform basic validation code
        pass

    def initialize(self, config):
        self.config = config
        self._load_modules()
        logger.info(self._module_registry)
        logger.info("initialized")
        self.provision_node()

    def _load_modules(self):
        package = importlib.import_module(self.base_package)
        package_path = package.__path__[0]

        for root, _, _ in os.walk(package_path):
            # Calculate the package name
            relative_path = os.path.relpath(root, package_path)
            if relative_path == ".":
                module_base = self.base_package
            else:
                module_base = f"{self.base_package}.{relative_path.replace(os.sep, '.')}"

            for loader, module_name, is_pkg in pkgutil.iter_modules([root]):
                full_module_name = f"{module_base}.{module_name}"
                importlib.import_module(full_module_name)

    def _build_script(self, all_templates, phase):
        # Create a named temporary file
        with tempfile.NamedTemporaryFile(mode="w+", delete=False) as temp_file:
            # Add a bash shebang
            temp_file.write("#!/bin/bash\n\n")
            for key in all_templates:
                temp_file.write("\n######## START %s #########\n" % key)
                temp_file.write(all_templates[key][phase])
                temp_file.write("\n######## END %s #########\n" % key)
        # python3 needs 0o
        os.chmod(temp_file.name, 0o755)
        logger.info(temp_file.name)
        return temp_file.name

    def _run_script(self, script_path):
        result = subprocess.run(["/bin/bash", "-lc", script_path],
                                capture_output=True, text=True)
        # Print the output and error (if any)
        logger.info("Output: %s", result.stdout)
        logger.info("Error: %s", result.stderr)
        logger.info("Return Code: %s", result.returncode)

    def provision_node(self):
        all_templates = {}

        for key in self.config:
            module, module_path = self._module_registry.get(key)
            context = self.config[key]
            templatedir = os.path.join(os.path.dirname(module_path), "templates")
            context["templatedir"] = templatedir
            logger.info(context)
            module_instance = module()
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
