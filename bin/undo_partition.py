#!/usr/bin/env python

import argparse, psycopg2, time

parser = argparse.ArgumentParser(description="This script calls either undo_partition(), undo_partition_time() or undo_partition_id depending on the value given for --type. A commit is done at the end of each --interval and/or emptied partition. Returns the total number of rows put into the parent. Automatically stops when last child table is empty.")
parser.add_argument('-p','--parent', required=True, help="Parent table of the partition set. (Required)")
parser.add_argument('-t','--type', choices=["time","id",], help="""Type of partitioning. Valid values are "time" and "id". Not setting this argument will use undo_partition() and work on any parent/child table set.""")
parser.add_argument('-c','--connection', default="host=localhost", help="""Connection string for use by psycopg to connect to your database. Defaults to "host=localhost".""")
parser.add_argument('-i','--interval', help="Value that is passed on to the undo partitioning function as p_batch_interval. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given. If -t value is not set, interval cannot be smaller than the partition interval and an entire partition is copied each batch.")
parser.add_argument('-b','--batch', type=int, default=0, help="How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and undo at most -b partition(s).  Script commits at the end of each individual batch. (NOT passed as p_batch_count to undo function). If not set, all data will be moved to the parent table in a single run of the script.")
parser.add_argument('-w','--wait', type=float, default=0, help="Cause the script to pause for a given number of seconds between commits (batches).")
parser.add_argument('-d', '--droptable', action="store_true", help="Switch setting for whether to drop child tables when they are empty. Do not set to just uninherit.")
parser.add_argument('-q', '--quiet', action="store_true", help="Switch setting to stop all output during and after partitioning undo.")
args = parser.parse_args() 

batch_count = 0
total = 0

conn = psycopg2.connect(args.connection)

cur = conn.cursor()
sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
cur.execute(sql)
partman_schema = cur.fetchone()[0]
cur.close()

cur = conn.cursor()

sql = "SELECT " + partman_schema + ".undo_partition"
if args.type != None:
    sql += "_" + args.type
sql += "(%s, p_keep_table := %s"
if args.interval != None:
    sql += ", p_batch_interval := %s"
sql += ")"

# Actual undo sql functions do not drop by default, so fix argument value to match that default
if args.droptable:
    keep_table = False
else:
    keep_table = True

while True:
    if args.interval != None:
        li = [args.parent, keep_table, args.interval]
    else:
        li = [args.parent, keep_table]
#    print cur.mogrify(sql, li)
    cur.execute(sql, li)
    result = cur.fetchone()
    conn.commit()
    if not args.quiet:
        print "Rows put into parent: " + str(result[0])
    total += result[0]
    batch_count += 1
    # If no rows left or given batch argument limit is reached
    if (result[0] == 0) or (args.batch > 0 and batch_count >= int(args.batch)):
        break
    if args.wait > 0:
        time.sleep(float(args.wait))

if not args.quiet:
    print total

cur.close()
conn.close()
