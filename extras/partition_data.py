#!/usr/bin/env python

import psycopg2, sys, getopt, time

help_string = """
This script calls either partition_data_time() or partition_data_id depending on the value given for --type.
A commit is done at the end of each --interval and/or fully created partition.
Returns the total number of rows moved to partitions. Automatically stops when parent is empty.

    --parent (-p):          Parent table an already created partition set. Required.\n
    --type (-t):            Type of partitioning. Valid values are "time" and "id". Required.\n
    --connection (-c):      Connection string for use by psycopg to connect to your database. Defaults to "host=localhost".
                            Highly recommended to use .pgpass file or environment variables to keep credentials secure.\n
    --interval (-i):        Value that is passed on to the partitioning function as p_batch_interval argument. 
                            Use this to set an interval smaller than the partition interval to commit data in smaller batches.
                            Defaults to the partition interval if not given.\n
    --batch (-b):           How many times to loop through the value given for --interval.
                            If --interval not set, will use default partition interval and make at most -b partition(s).
                            Script commits at the end of each individual batch. (NOT passed as p_batch_count to partitioning function).
                            If not set, all data in the parent table will be partitioned in a single run of the script.\n
    --wait (-w):            Cause the script to pause for a given number of seconds between commits (batches).\n
    --schema (-s):          The schema that pg_partman was installed to. Default is "partman".\n
    --quiet (-q):           Switch setting to stop all output during and after partitioning.

Example to partition all data in a parent table. Commit after each partition is made.\n
        python partition_data.py -c "host=localhost dbname=mydb" -p schema.parent_table -t time\n
Example to partition by id in smaller intervals and pause between them for 5 seconds (assume >100 partition interval)\n
        python partition_data.py -p schema.parent_table -t id -i 100 -w 5\n
Example to partition by time in smaller intervals for at most 10 partitions in a single run (assume monthly partition interval)\n
        python partition_data.py -p schema.parent_table -t time -i "1 week" -b 10
"""

try: 
    opts, args = getopt.getopt(sys.argv[1:], "hqt:p:c:b:i:s:w:", ["help","quiet","type=","parent=","connection=","batch=","interval=","schema=", "wait"])
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
    elif opt in ("-w", "--wait"):
        arg_wait = arg
    elif opt in ("-s", "--schema"):
        arg_schema = arg
    elif opt in ("-q", "--quiet"):
        arg_quiet = 1

if arg_parent == "":
    print "--parent (-p) argument is required"
    sys.exit(2)
if arg_type == "":
    print "--type (-t) argument is required"
    sys.exit(2)

conn = psycopg2.connect(arg_connection)

cur = conn.cursor()

sql = "SELECT " + arg_schema + ".partition_data_" + arg_type + "(%s"
if arg_interval != "":
    sql += ", p_batch_interval := %s"
sql += ")"

while True:
    if arg_interval != "":
        li = [arg_parent, arg_interval]
    else:
        li = [arg_parent]
#    print cur.mogrify(sql, li)
    cur.execute(sql, li)
    result = cur.fetchone()
    conn.commit()
    if arg_quiet == 0:
        print "Rows moved: " + str(result[0])
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
