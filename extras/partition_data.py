#!/usr/bin/env python

import psycopg2, sys, argparse, time

"""
Examples until we get some real docs:
Example to partition all data in a parent table. Commit after each partition is made.\n
        python partition_data.py -c "host=localhost dbname=mydb" -p schema.parent_table -t time\n
Example to partition by id in smaller intervals and pause between them for 5 seconds (assume >100 partition interval)\n
        python partition_data.py -p schema.parent_table -t id -i 100 -w 5\n
Example to partition by time in smaller intervals for at most 10 partitions in a single run (assume monthly partition interval)\n
            python partition_data.py -p schema.parent_table -t time -i "1 week" -b 10
"""

parser = argparse.ArgumentParser(description="This script calls either partition_data_time() or partition_data_id() depending on the value given for --type. A commit is done at the end of each --interval and/or fully created partition. Returns the total number of rows moved to partitions. Automatically stops when parent is empty.")
parser.add_argument('-p','--parent', help="Parent table of an already created partition set. Required.")
parser.add_argument('-t','--type', help="""Type of partitioning. Valid values are "time" and "id". Required.""", choices=["time","id",])
parser.add_argument('-c','--connection', help="""Connection string for use by psycopg to connect to your database. Defaults to "host=localhost". Highly recommended to use .pgpass file or environment variables to keep credentials secure.""", default="host=localhost")
parser.add_argument('-i','--interval',help="""Value that is passed on to the partitioning function as p_batch_interval argument. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given.""")
parser.add_argument('-b','--batch',type=int,help="""How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and make at most -b partition(s). Script commits at the end of each individual batch. (NOT passed as p_batch_count to partitioning function). If not set, all data in the parent table will be partitioned in a single run of the script.""",default=0)
parser.add_argument('-w','--wait',type=float,help="Cause the script to pause for a given number of seconds between commits (batches) to reduce write load",default=0)
parser.add_argument('-l','--lockwait', type=float, help="Have a lock timeout of this many seconds on the data move. If a lock is not obtained, that batch will be tried again.", default=0)
parser.add_argument('--lockwait_tries', type=int, help="Number of times to allow a lockwait to time out before giving up on the partitioning.  Defaults to 10", default=10)
parser.add_argument('-s','--schema',help="""The schema that pg_partman was installed to. Default is "partman".""",default="partman")
parser.add_argument('-q','--quiet',help="Switch setting to stop all output during and after partitioning for use in cron jobs", action="store_true")
args = parser.parse_args()

if args.parent == "":
    print "--parent (-p) argument is required"
    sys.exit(2)
if args.type == "":
    print "--type (-t) argument is required"
    sys.exit(2)

batch_count = 0
total = 0
lockwait_count = 0

conn = psycopg2.connect(args.connection)

cur = conn.cursor()

sql = "SELECT " + args.schema + ".partition_data_" + args.type + "(%s"
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
