# #!/usr/bin/env python3
# # Copyright (c) YugaByte, Inc.

# import argparse
# import logging
# import os
# import shutil
# import glob

# from ybops.utils import init_env, log_message, get_release_file, get_devops_home
# from ybops.common.exceptions import YBOpsRuntimeError

# """This script packages the yba-cli go linux binary executable (yba) into the
#    Yugabundle archive.
#    Needed in order to perform end to end testing of yba-cli.
# """

# parser = argparse.ArgumentParser()
# parser.add_argument('--destination', help='Copy release to Destination folder.')
# parser.add_argument('--package', help='yba_cli.tar.gz package to copy to destination.')

# args = parser.parse_args()

# try:
#     init_env(logging.INFO)
#     script_dir_yba_cli = os.path.dirname(os.path.realpath(__file__))
#     # Using "yba_cli" so that the release package can be processed correctly in the build
#     # job.
#     release_file = get_release_file(
#         script_dir_yba_cli,
#         "yba_cli",
#         os_type="linux",
#         arch_type="x86_64")

#     shutil.copyfile(args.package, release_file)
#     if args.destination:
#         if not os.path.exists(args.destination):
#             raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
#         shutil.copy(release_file, args.destination)

# except (OSError, shutil.SameFileError) as e:
#     log_message(logging.ERROR, e)
#     raise e
