#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import argparse
import logging
import os
import subprocess

from datetime import datetime

# Given a set of servers and the private access key, this script copies all the tserver and master logs
# to a latest timestamp based directory.

def main():
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    parser = argparse.ArgumentParser()
    parser.add_argument('--servers', help='Comma separated host_ips to copy tserver/master files.',
                        required=True)
    parser.add_argument('--private_key', help='The private access key to scp from the servers.',
                        required=True)

    args = parser.parse_args()

    out_dir = os.path.join("/home/centos/save/", timestamp)
    os.makedirs(out_dir)

    scp_base = ["sudo", "/usr/bin/scp", "-oStrictHostKeyChecking=no", "-P", "54422", "-i", args.private_key]
    user_id = "yugabyte@"
    master_files=":/home/yugabyte/master/logs/yb-master.*log*"
    tserver_files=":/home/yugabyte/tserver/logs/yb-tserver.*log*"

    for ip in args.servers.split(","):
        scp_cmd = scp_base + [user_id + ip + master_files, out_dir]
        subprocess.check_output(scp_cmd)
        scp_cmd = scp_base + [user_id + ip + tserver_files, out_dir]
        subprocess.check_output(scp_cmd)

    logging.info("Files copied to : {}".format(out_dir))

if __name__ == "__main__":
    main()
