import importlib.metadata
import jinja2
import logging
import config
import setup_logger
import ynp_main
import config
import sys
import argparse
import yaml
import pprint

# Get all installed packages
installed_packages = importlib.metadata.distributions()
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="YNP: Node Provisioning Tool")
    parser.add_argument('--provision_config',
                        type=str,
                        required=True,
                        help='Path to the provision configuration file')
    args = parser.parse_args()
    node_provision_config = args.provision_config

    with open(node_provision_config) as f:
        npc = yaml.safe_load(f)

    setup_logger.setup_logger()
    # Get a logger for the main module
    logger = logging.getLogger(__name__)
    logger.debug("Node provision config %s" % pprint.pformat(npc))
    # initialize config.
    conf = config.parse_config()
    # log python version.
    python_version = sys.version
    logger.info("Python Version: %s", python_version)

    # log a list of installed packages.
    for package in installed_packages:
        logger.info("%s -- %s" % (package.metadata["Name"], package.version))

    default_config = conf.pop("DEFAULT")
    # initialized base.ynp
    ynp = ynp_main.YugabyteUniverseNodeProvisioner()
    ynp.initialize(conf)
