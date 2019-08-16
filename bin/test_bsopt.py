#!/usr/bin/python3

import os
import subprocess
import threading
import time
import urllib.request
import json
import sys

# YugaByte directories
YUGABYTE_DIR = "/home/centos/code/yugabyte"
YBSAMPLEAPPS_DIR = "/home/centos/code/yb-sample-apps"

# Number of tablets to use
NUM_TABLETS = 2

# Time to run the CQL test for (seconds)
CQL_TEST_TIME = 10*60

# Number of trials, both for the optimization on and off
NUM_TRIALS = 15

def test_cluster(opt_on):
	# Start the CQL stress test
	args = ["java", "-jar", YBSAMPLEAPPS_DIR + "/target/yb-sample-apps.jar", "--workload", "CassandraBatchKeyValue",\
		"--nodes", "127.0.0.1:9042", "--num_threads_read", "1", "--num_threads_write", "1",\
		"--num_unique_keys", "100000", "--nouuid", "--value_size", "1024", "--batch_size", "64"]
	proc = subprocess.Popen(args)

	# After time is up, kill the test
	timer = threading.Timer(CQL_TEST_TIME, lambda p: p.kill(), (proc,))
	timer.start()
	proc.wait()
	timer.cancel()

	# Use yb-admin to flush all writes to RocksDB
	os.system(YUGABYTE_DIR + "/build/latest/bin/yb-admin -master_addresses 127.0.0.1 flush_table ybdemo_keyspace cassandrakeyvalue 60")

	# Restart the cluster
	os.system(YUGABYTE_DIR + "/bin/yb-ctl stop")
	os.system(YUGABYTE_DIR + "/bin/yb-ctl start --tserver_flags \"skip_flushed_entries=" + str(opt_on).lower() + "\"")
	time.sleep(10)

	# Get the bootstrap time
	for i in range(0, 10):
		metrics = json.loads(urllib.request.urlopen("http://127.0.0.1:9000/metrics").read())
		for group in metrics:
			if group["type"] == "server":
				smetrics = group["metrics"]
				for metric in smetrics:
					if metric["name"] == "ts_bootstrap_time":
						print(metric)
						sys.stdout.flush()

						if metric["total_count"] == NUM_TABLETS:
							return metric["total_sum"]
						else:
							time.sleep(10)

	# Metric not present (SHOULD NEVER REACH HERE)
	return -1

# Destroy and remake the cluster from scratch
def remake_cluster():
	os.system(YUGABYTE_DIR + "/bin/yb-ctl stop")
	os.system(YUGABYTE_DIR + "/bin/yb-ctl destroy")
	os.system(YUGABYTE_DIR + "/bin/yb-ctl --num_shards_per_tserver " + str(NUM_TABLETS) + " create")
	time.sleep(5)

# Run our trials
for i in range(NUM_TRIALS):
	# Trial with optimization
	remake_cluster()
	print("OPTIMIZE_YES: %d" % test_cluster(True))
	sys.stdout.flush()

	# Trial without optimization
	remake_cluster()
	print("OPTIMIZE_NO:  %d" % test_cluster(False))
	sys.stdout.flush()
