import pkg_resources
from pkg_resources import DistributionNotFound, VersionConflict

modules = open('../python3_requirements_frozen.txt', 'r').readlines()

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
