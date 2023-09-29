#!/usr/bin/env python

import argparse
import glob
import subprocess
import os
import logging
from multiprocessing import Pool


LDB_PATH = "/home/yugabyte/tserver/bin/ldb"

parser = argparse.ArgumentParser()
parser.add_argument('--fs_data_dirs', type=str, help="Path to data directories")
parser.add_argument('--ldb_path', type=str, default=LDB_PATH, help="Path to ldb binary")
parser.add_argument('--mode', type=int, default=1, choices=[1,2], help="Mode to run ldb in: 1=checkconsistency, 2=scan --only_verify_checksum")
parser.add_argument('--uuid', type=str, default=None, help="Table or tablet uuid to check")
parser.add_argument('--num_processes', type=int, default=1, help="Number of processes to run in parallel")
args = parser.parse_args()

# Logger for STDOUT
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

## Create file handler which logs even debug messages
file_handler = logging.FileHandler("checkconsistency.log")
file_handler.setLevel(logging.DEBUG)

## Create console handler with a higher log level
console_handler = logging.StreamHandler()
console_handler.setLevel(logging.INFO)

## Create formatter and add it to the handlers
formatter = logging.Formatter('%(asctime)s:[%(levelname)s]:%(message)s')
file_handler.setFormatter(formatter)
console_handler.setFormatter(formatter)

## Add the handlers to the logger
logger.addHandler(file_handler)
logger.addHandler(console_handler)


def get_docdb_paths():
    paths = []
    for dir in args.fs_data_dirs.split(","):
        paths.extend(glob.glob("{}/yb-data/tserver/data/rocksdb/table-*/tablet-*".format(dir)))
    if args.uuid:
        # Get only the paths that match the tablet uuid
        paths = [path for path in paths if args.tablet in path]
    
    # Remove snapshots
    paths = [path for path in paths if not path.endswith("snapshots")]
    return paths

def run_ldb(path):
    if args.mode == 1:
        cmd_list = [args.ldb_path, "checkconsistency"]
    elif args.mode == 2:
        cmd_list = [args.ldb_path, "scan", "--only_verify_checksums"]
    cmd_list = cmd_list + ["--db={}".format(path)]
    logger.debug("Running: {}".format(" ".join(cmd_list)))
    proc = subprocess.Popen(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    if proc.returncode != 0:
        if not os.path.exists("checkconsistency_errors"):
            os.makedirs("checkconsistency_errors")
        filename = path.split("/")[-1]
        with open("checkconsistency_errors/{}.txt".format(filename), "w") as f:
            f.write(str(stderr))
        logger.error("{}".format(path))
    else:
        logger.info("OK: {}".format(path))
    

if __name__ == "__main__":
    paths = get_docdb_paths()
    pool = Pool(processes=args.num_processes)
    pool.map(run_ldb, paths)
    pool.close()
    pool.join()
