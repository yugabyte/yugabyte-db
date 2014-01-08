#!/usr/bin/env python

import argparse, psycopg2, time, sys

parser = argparse.ArgumentParser(description="This script calls either partition_data_time() or partition_data_id() depending on the value given for --type. A commit is done at the end of each --interval and/or fully created partition. Returns the total number of rows moved to partitions. Automatically stops when parent is empty. See docs for examples.")
parser.add_argument('-p','--parent', required=True, help="Parent table of an already created partition set. (Required)")
parser.add_argument('-t','--type', choices=["time","id",], required=True, help="""Type of partitioning. Valid values are "time" and "id". (Required)""")
parser.add_argument('-c','--connection', default="host=localhost", help="""Connection string for use by psycopg to connect to your database. Defaults to "host=localhost".""")
parser.add_argument('-i','--interval', help="Value that is passed on to the partitioning function as p_batch_interval argument. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given.")
parser.add_argument('-b','--batch', default=0, type=int, help="""How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and make at most -b partition(s). Script commits at the end of each individual batch. (NOT passed as p_batch_count to partitioning function). If not set, all data in the parent table will be partitioned in a single run of the script.""")
parser.add_argument('-w','--wait', default=0, type=float, help="Cause the script to pause for a given number of seconds between commits (batches) to reduce write load")
parser.add_argument('-l','--lockwait', default=0, type=float, help="Have a lock timeout of this many seconds on the data move. If a lock is not obtained, that batch will be tried again.")
parser.add_argument('--lockwait_tries', default=10, type=int, help="Number of times to allow a lockwait to time out before giving up on the partitioning.  Defaults to 10")
parser.add_argument('-q','--quiet', action="store_true", help="Switch setting to stop all output during and after partitioning for use in cron jobs")
args = parser.parse_args()

batch_count = 0
total = 0
lockwait_count = 0

conn = psycopg2.connect(args.connection)

cur = conn.cursor()
sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
cur.execute(sql)
partman_schema = cur.fetchone()[0]
cur.close()

cur = conn.cursor()

sql = "SELECT " + partman_schema + ".partition_data_" + args.type + "(%s"
if args.interval != "":
    sql += ", p_batch_interval := %s"
sql += ", p_lock_wait := %s)"

while True:
    if args.interval != "":
        li = [args.parent, args.interval, args.lockwait]
    else:
        li = [args.parent, args.lockwait]
#    print cur.mogrify(sql, li)
    cur.execute(sql, li)
    result = cur.fetchone()
    conn.commit()
    if not args.quiet:
        if result[0] > 0:
            print "Rows moved: " + str(result[0])
        elif result[0] == -1:
            print "Unable to obtain lock, trying again"
    # if lock wait timeout, do not increment the counter
    if result[0] <> -1:
        batch_count += 1
        total += result[0]
        lockwait_count = 0
    else:
        lockwait_count += 1
        if lockwait_count > args.lockwait_tries:
            print "quitting due to inability to get lock on next rows to be moved"
            print "total rows moved: %d" % total
            break
    # If no rows left or given batch argument limit is reached
    if (result[0] == 0) or (args.batch > 0 and batch_count >= int(args.batch)):
        break
    time.sleep(args.wait)

if not args.quiet:
    print "total rows moved: %d" % total

conn.close()
