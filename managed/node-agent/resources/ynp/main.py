import logging
import sys
import argparse
import pkg_resources
import os
import pprint
import json
from configs.config import parse_config, validate_config
from configs import setup_logger
from executor import Executor
import yaml


def get_absolute_path(relative_path):
    # Get the absolute path of the file
    absolute_path = os.path.realpath(relative_path)
    return absolute_path


def parse_arguments():
    parser = argparse.ArgumentParser(description="YNP: Yugabyte Node Provisioning Tool")
    parser.add_argument('--command', default="provision", required=False, help='Command to execute')
    parser.add_argument('--specific_module', default=None, required=False,
                        help='Specific module to execute')
    parser.add_argument('--config_file',
                        default="./node-agent-provision.yaml",
                        help='Path to the ynp configuration file')
    parser.add_argument('--preflight_check', action="store_true",
                        help='Execute the pre-flight check on the node')
    parser.add_argument('--extra_vars', default='{}',
                        help='Path to the JSON file containing extra variables \
                        required for execution.')
    parser.add_argument('--dry-run', action="store_true",
                        help='Render Execution Scripts without executing them for dry-run')
    args = parser.parse_args()

    # Read extra_vars from JSON file if the path is provided
    if args.extra_vars:
        args.extra_vars = load_json_or_file(args.extra_vars)

    return args


def load_json_or_file(json_or_path):
    # Check if the input is a file path and if it exists
    if os.path.isfile(json_or_path):
        try:
            with open(json_or_path, 'r') as file:
                return json.load(file)
        except Exception as e:
            raise e
    else:
        # Try parsing as a JSON string
        try:
            return json.loads(json_or_path)
        except json.JSONDecodeError as e:
            raise ValueError(f"Invalid JSON or file path for extra_vars: {e}")

    return parser.parse_args()


def log_installed_packages(logger):
    installed_packages = pkg_resources.working_set
    for package in installed_packages:
        logger.info("%s -- %s", package.project_name, package.version)


def main():
    args = parse_arguments()
    try:
        with open(get_absolute_path(args.config_file)) as f:
            ynp_config = yaml.safe_load(f)
            # Set default values.
            extra = ynp_config.setdefault("extra", {})
            extra.setdefault("cloud_type", "onprem")
            extra.setdefault("is_cloud", False)
            extra.setdefault("is_ybm", False)
            for key, value in args.extra_vars.items():
                # If the section already exists in ynp_config, update it
                if key in ynp_config:
                    ynp_config[key].update(value)
                # If the section does not exist, add it as a new section
                else:
                    ynp_config[key] = value
    except Exception as e:
        print("Parsing config file failed with ", e)
        raise
    conf = parse_config(ynp_config)
    validate_config(conf)
    setup_logger.setup_logger(conf)

    logger = logging.getLogger(__name__)
    logger.debug("YNP config %s" % pprint.pformat(ynp_config))

    logger.info("Config here: %s", conf)
    logger.info("Python Version: %s", sys.version)
    log_installed_packages(logger)

    executor = Executor(conf, args)
    executor.exec()


if __name__ == "__main__":
    main()
