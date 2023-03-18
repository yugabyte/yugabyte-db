import pkg_resources
import argparse
from pkg_resources import DistributionNotFound, VersionConflict

parser = argparse.ArgumentParser()
parser.add_argument("-r", "--requirements", help="Source Python file", required=True)
args = parser.parse_args()

modules = open(args.requirements, 'r').readlines()

for module in modules:
    try:
        pkg_resources.require(module)
        print(module.strip() + " imported succesfully!")
    except DistributionNotFound:
        print("Distribution Not Found! " + "\"" + module.strip() + "\"" + " unable to be executed.")
        break
    except VersionConflict:
        print("Version Conflict! " + "\"" + module.strip() + "\"" + " unable to be executed.")
        break
