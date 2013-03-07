#!/usr/bin/env python

import psycopg2, sys, getopt, time

help_string = """
This script calls either undo_partition(), undo_partition_time() or undo_partition_id depending on the value given for --type.
A commit is done at the end of each --interval and/or emptied partition.
Returns the total number of rows put into the to parent. Automatically stops when last child table is empty.

    --parent (-p):          Parent table of the partition set. Required.\n
    --type (-t):            Type of partitioning. Valid values are "time" and "id". 
                            Not setting this argument will use undo_partition() and work on any parent/child table set.\n
    --connection (-c):      Connection string for use by psycopg to connect to your database. Defaults to "host=localhost".
                            Highly recommended to use .pgpass file or environment variables to keep credentials secure.\n
    --interval (-i):        Value that is passed on to the partitioning function as p_batch_interval. 
                            Use this to set an interval smaller than the partition interval to commit data in smaller batches.
                            Defaults to the partition interval if not given.\n
    --batch (-b):           How many times to loop through the value given for --interval.
                            If --interval not set, will use default partition interval and undo at most -b partition(s).
                            Script commits at the end of each individual batch. (NOT passed as p_batch_count to undo function).
                            If not set, all data will be moved to the parent table in a single run of the script.\n
    --wait (-w):            Cause the script to pause for a given number of seconds between commits (batches).\n
    --schema (-s):          The schema that pg_partman was installed to. Default is "partman".\n
    --droptable (-d):       Switch setting for whether to drop child tables when they are empty. Leave off option to just uninherit.\n
    --quiet (-q):           Switch setting to stop all output during and after partitioning undo.

Example to unpartition all data into the parent table. Commit after each partition is undone.\n
        python undo_partition.py -c "host=localhost dbname=mydb" -p schema.parent_table -t time\n
Example to unpartition by id in smaller intervals and pause between them for 5 seconds (assume >100 partition interval)\n
        python undo_partition.py -p schema.parent_table -t id -i 100 -w 5\n
Example to unpartition by time in smaller intervals for at most 10 partitions in a single run (assume monthly partition interval)\n
        python undo_partition.py -p schema.parent_table -t time -i "1 week" -b 10
"""

try: 
    opts, args = getopt.getopt(sys.argv[1:], "hqdt:p:c:b:i:s:w:", ["help","quiet","drop","type=","parent=","connection=","batch=","interval=","schema=", "wait"])
except getopt.GetoptError:
    print "Invalid argument"
    print help_string
    sys.exit(2)

arg_parent = ""
arg_type = ""
arg_batch = ""
arg_interval = ""
arg_wait = ""
arg_connection = "host=localhost"
arg_schema = "partman"
arg_quiet = 0
batch_count = 0
arg_drop = "true"
total = 0
for opt, arg in opts:
    if opt in ("-h", "--help"):
        print help_string
        sys.exit()
    elif opt in ("-p", "--parent"):
        arg_parent = arg
    elif opt in ("-t", "--type"):
        arg_type = arg
        if arg_type not in ("time", "id"):
            print "--type (-t) must be one of the following: time, id"
            sys.exit(2)
    elif opt in ("-c", "--connection"):
        arg_connection = arg
    elif opt in ("-b", "--batch"):
        arg_batch = arg
    elif opt in ("-i", "--interval"):
        arg_interval = arg
    elif opt in ("-d", "--droptable"):
        arg_drop = "false";
    elif opt in ("-w", "--wait"):
        arg_wait = arg
    elif opt in ("-s", "--schema"):
        arg_schema = arg
    elif opt in ("-q", "--quiet"):
        arg_quiet = 1

if arg_parent == "":
    print "--parent (-p) argument is required"
    sys.exit(2)

conn = psycopg2.connect(arg_connection)

cur = conn.cursor()

sql = "SELECT " + arg_schema + ".undo_partition"
if arg_type != "":
    sql += "_" + arg_type
sql += "(%s, p_keep_table := %s"
if arg_interval != "":
    sql += ", p_batch_interval := %s"
sql += ")"

while True:
    if arg_interval != "":
        li = [arg_parent, arg_drop, arg_interval]
    else:
        li = [arg_parent, arg_drop]
#    print cur.mogrify(sql, li)
    cur.execute(sql, li)
    result = cur.fetchone()
    conn.commit()
    if arg_quiet == 0:
        print "Rows put into parent: " + str(result[0])
    total += result[0]
    batch_count += 1
    # If no rows left or given batch argument limit is reached
    if (result[0] == 0) or (arg_batch != "" and batch_count >= int(arg_batch)):
        break
    if arg_wait != "":
        time.sleep(float(arg_wait))

if arg_quiet == 0:
    print total

cur.close()
conn.close()
