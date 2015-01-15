#!/usr/bin/env python

import argparse, psycopg2, signal, sys, time

partman_version = "1.8.0"

parser = argparse.ArgumentParser(description="This script calls either undo_partition(), undo_partition_time() or undo_partition_id depending on the value given for --type. A commit is done at the end of each --interval and/or emptied partition. Returns the total number of rows put into the parent. Automatically stops when last child table is empty.", epilog="NOTE: To help avoid heavy load and contention during the undo, autovacuum is turned off for the parent table and all child tables when this script is run. When the undo is complete, autovacuum is set back to its default for the parent. Any child tables left behind will still have it turned off.")
parser.add_argument('-p','--parent', help="Parent table of the partition set. (Required)")
parser.add_argument('-t','--type', choices=["time","id",], help="""Type of partitioning. Valid values are "time" and "id". Not setting this argument will use undo_partition() and work on any parent/child table set.""")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('-i','--interval', help="Value that is passed on to the undo partitioning function as p_batch_interval. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given. If -t value is not set, interval cannot be smaller than the partition interval and an entire partition is copied each batch.")
parser.add_argument('-b','--batch', type=int, default=0, help="How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and undo at most -b partition(s).  Script commits at the end of each individual batch. (NOT passed as p_batch_count to undo function). If not set, all data will be moved to the parent table in a single run of the script.")
parser.add_argument('-d', '--droptable', action="store_true", help="Switch setting for whether to drop child tables when they are empty. Do not set to just uninherit.")
parser.add_argument('-w','--wait', type=float, default=0, help="Cause the script to pause for a given number of seconds between commits (batches).")
parser.add_argument('-l','--lockwait', default=0, type=float, help="Have a lock timeout of this many seconds on the data move. If a lock is not obtained, that batch will be tried again.")
parser.add_argument('--lockwait_tries', default=10, type=int, help="Number of times to allow a lockwait to time out before giving up on the partitioning.  Defaults to 10")
parser.add_argument('--autovacuum_on', action="store_true", help="Turning autovacuum off requires a brief lock to ALTER the table property. Set this option to leave autovacuum on and avoid the lock attempt.")
parser.add_argument('-q', '--quiet', action="store_true", help="Switch setting to stop all output during and after partitioning undo.")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
parser.add_argument('--debug', action="store_true", help="Show additional debugging output")
args = parser.parse_args() 


def close_conn(conn):
    conn.close()


def create_conn():
    conn = psycopg2.connect(args.connection)
    conn.autocommit = True
    return conn


def get_partman_schema(conn):
    cur = conn.cursor()
    sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(sql)
    partman_schema = cur.fetchone()[0]
    cur.close()
    return partman_schema


def print_version():
    print(partman_version)
    sys.exit()


def reset_autovacuum(conn, table):
    cur = conn.cursor()
    sql = "ALTER TABLE " + args.parent + " RESET (autovacuum_enabled, toast.autovacuum_enabled)"
    if not args.quiet:
        print("Attempting to reset autovacuum for old parent table...")
    if args.debug:
        print(cur.mogrify(sql))
    cur.execute(sql)
    sql = "SELECT * FROM " + partman_schema + ".show_partitions(%s)"
    if args.debug:
        print(cur.mogrify(sql, [args.parent]))
    cur.execute(sql, [args.parent])
    result = cur.fetchall()
    for r in result:
        sql = "ALTER TABLE " + r[0] + " RESET (autovacuum_enabled, toast.autovacuum_enabled)"
        if args.debug:
            print(cur.mogrify(sql))
        cur.execute(sql)
    print("\t... Success!")
    cur.close()


def sigint_handler(signum, frame):
    if is_autovac_off == True:
        reset_autovacuum(conn, partman_schema)
        sys.exit(2)

def turn_off_autovacuum(conn, partman_schema):
    cur = conn.cursor()
    sql = "ALTER TABLE " + args.parent + " SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)"
    if not args.quiet:
        print("Attempting to turn off autovacuum for partition set...")
    if args.debug:
        print(cur.mogrify(sql))
    cur.execute(sql)
    sql = "SELECT * FROM " + partman_schema + ".show_partitions(%s)"
    if args.debug:
        print(cur.mogrify(sql, [args.parent]))
    cur.execute(sql, [args.parent])
    result = cur.fetchall()
    for r in result:
        sql = "ALTER TABLE " + r[0] + " SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)"
        if args.debug:
            print(cur.mogrify(sql))
        cur.execute(sql)
    print("\t... Success!")
    cur.close()


def undo_partition_data(conn, partman_schema):
    batch_count = 0
    total = 0
    lockwait_count = 0

    cur = conn.cursor()

    sql = "SELECT " + partman_schema + ".undo_partition"
    if args.type != None:
        sql += "_" + args.type
    sql += "(%s, p_keep_table := %s"
    if args.interval != None:
        sql += ", p_batch_interval := %s"
    sql += ", p_lock_wait := %s"
    sql += ")"

    # Actual undo sql functions do not drop by default, so fix argument value to match that default
    if args.droptable:
        keep_table = False
    else:
        keep_table = True

    while True:
        if args.interval != None:
            li = [args.parent, keep_table, args.interval, args.lockwait]
        else:
            li = [args.parent, keep_table, args.lockwait]
        if args.debug:
            print(cur.mogrify(sql, li))
        cur.execute(sql, li)
        result = cur.fetchone()
        conn.commit()
        if not args.quiet:
            if result[0] > 0:
                print("Rows moved into parent: " + str(result[0]))
            elif result[0] == -1:
                print("Unable to obtain lock, trying again (" + str(lockwait_count+1) + ")")
                print(conn.notices[-1])
        # if lock wait timeout, do not increment the counter
        if result[0] != -1:
            batch_count += 1
            total += result[0]
            lockwait_count = 0
        else:
            lockwait_count += 1
            if lockwait_count > args.lockwait_tries:
                print("Quitting due to inability to get lock on table/rows for migration to parent")
                break
        # If no rows left or given batch argument limit is reached
        if (result[0] == 0) or (args.batch > 0 and batch_count >= int(args.batch)):
            break
        if args.wait > 0:
            time.sleep(args.wait)

    return total


def vacuum_parent(conn):
    cur = conn.cursor()
    sql = "VACUUM ANALYZE " + args.parent
    if args.debug:
        print(cur.mogrify(sql))
    if not args.quiet:
        print("Running vacuum analyze on parent table...")
    cur.execute(sql)
    cur.close()


if __name__ == "__main__":
    if args.version:
        print_version()

    if args.parent == None:
        print("-p/--parent option is required")
        sys.exit(2)

    if args.parent.find(".") < 0:
        print("ERROR: Parent table must be schema qualified")
        sys.exit(2)

    is_autovac_off = False
    signal.signal(signal.SIGINT, sigint_handler)
    conn = create_conn()
    partman_schema = get_partman_schema(conn)

    if not args.autovacuum_on:
        turn_off_autovacuum(conn, partman_schema)
        is_autovac_off = True

    total = undo_partition_data(conn, partman_schema)

    if not args.quiet:
        print("Total rows moved: %d" % total)

    vacuum_parent(conn)

    if not args.autovacuum_on:
        reset_autovacuum(conn, partman_schema)
        is_autovac_off = False

    close_conn(conn)
