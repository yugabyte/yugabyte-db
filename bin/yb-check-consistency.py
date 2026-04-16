#!/usr/bin/env python

import argparse
import glob
import subprocess


LDB_PATH = "/home/yugabyte/tserver/bin/ldb"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fs_data_dirs', type=str, help="Path to data directories")
    parser.add_argument('--ldb_path', type=str, default=LDB_PATH, help="Path to ldb binary")
    args = parser.parse_args()
    for d in args.fs_data_dirs.split(","):
        print("Checking dir: {}".format(d))
        paths = glob.glob("{}/yb-data/tserver/data/rocksdb/table-*/tablet-*".format(d))
        for p in paths:
            # Ignore snapshots.
            if p.endswith("snapshots"):
                continue
            cmd_list = [args.ldb_path, "--db={}".format(p), "checkconsistency"]
            proc = subprocess.Popen(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = proc.communicate()
            if proc.returncode != 0:
                print(stderr)
            else:
                print("OK -- {}".format(p))


if __name__ == "__main__":
    main()
