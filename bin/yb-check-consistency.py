#!/usr/bin/env python

import argparse
import glob
import subprocess
import os


LDB_PATH = "/home/yugabyte/tserver/bin/ldb"

def print_colored_status(status, filename):
    if status == "OK":
        color_code = 32  # 32 is the ANSI code for green
    elif status == "ERROR":
        color_code = 31  # 31 is the ANSI code for red
    else:
        color_code = 0  # Default color (normal)

    colored_status = "\033[{0}m{1}\033[0m".format(color_code, status)
    print(colored_status + " - " + filename)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fs_data_dirs', type=str, help="Path to data directories")
    parser.add_argument('--ldb_path', type=str, default=LDB_PATH, help="Path to ldb binary")
    parser.add_argument('--mode', type=int, default=1, choices=[1,2], help="Mode to run ldb in: 1=checkconsistency, 2=scan --only_verify_checksum")
    
    args = parser.parse_args()
    for d in args.fs_data_dirs.split(","):
        print("Checking dir: {}".format(d))
        paths = glob.glob("{}/yb-data/tserver/data/rocksdb/table-*/tablet-*".format(d))
        for p in paths:
            # Ignore snapshots.
            if p.endswith("snapshots"):
                continue
            cmd_list = [args.ldb_path, "--db={}".format(p)]
            if args.mode == 1:
                cmd_list.append("checkconsistency")
            if args.mode == 2:
                cmd_list.append("scan")
                cmd_list.append("--only_verify_checksums")
            proc = subprocess.Popen(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = proc.communicate()
            if proc.returncode != 0:
                if not os.path.exists("checkconsistency_errors"):
                    os.makedirs("checkconsistency_errors")
                filename = p.split("/")[-1]
                with open("checkconsistency_errors/{}.txt".format(filename), "w") as f:
                    f.write(stderr)
                print_colored_status("ERROR", p)
            else:
                print_colored_status("OK", p)


if __name__ == "__main__":
    main()
