import logging
import sys
import argparse
import importlib.metadata
import os
import yaml
import pprint

from configs.config import parse_config
from configs import setup_logger
from executor import Executor


def get_absolute_path(relative_path):
    # Get the absolute path of the file
    absolute_path = os.path.realpath(relative_path)
    return absolute_path


def parse_arguments():
    parser = argparse.ArgumentParser(description="YNP: Yugabyte Node Provisioning Tool")
    parser.add_argument('--command', default="provision", required=False, help='Command to execute')
    parser.add_argument('--config_file', default="./node-agent-provision.yaml",
                        help='Path to the ynp configuration file')
    parser.add_argument('--preflight_check', action="store_true",
                        help='Execute the pre-flight check on the node')
    return parser.parse_args()


def log_installed_packages(logger):
    installed_packages = importlib.metadata.distributions()
    for package in installed_packages:
        logger.info("%s -- %s", package.metadata["Name"], package.version)


def main():
    args = parse_arguments()
    try:
        with open(get_absolute_path(args.config_file)) as f:
            ynp_config = yaml.safe_load(f)
    except Exception as e:
        print("Parsing YAML failed with ", e)
        raise
    conf = parse_config(ynp_config)
    setup_logger.setup_logger(conf)

    logger = logging.getLogger(__name__)
    logger.debug("YNP config %s" % pprint.pformat(ynp_config))

    conf = parse_config(ynp_config)
    logger.info("Config here: %s", conf)
    logger.info("Python Version: %s", sys.version)
    log_installed_packages(logger)

    executor = Executor(conf, args)
    executor.exec()


if __name__ == "__main__":
    main()
