#!/usr/bin/env python

import argparse, psycopg2, time, signal, sys

partman_version = "2.0.0"

parser = argparse.ArgumentParser(description="This script calls either partition_data_time() or partition_data_id() depending on the value given for --type. A commit is done at the end of each --interval and/or fully created partition. Returns the total number of rows moved to partitions. Automatically stops when parent is empty. See docs for examples.", epilog="NOTE: To help avoid heavy load and contention during partitioning, autovacuum is turned off for the entire partition set when this script is run. When partitioning is complete, autovacuum is set back to its default value and the parent table is vacuumed.")
parser.add_argument('-p','--parent', help="Parent table of an already created partition set. (Required)")
parser.add_argument('-t','--type', choices=["time","id",], help="""Type of partitioning. Valid values are "time" and "id". (Required)""")
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket). Note that two connections are required if allowing autovacuum to be turned off.""")
parser.add_argument('-i','--interval', help="Value that is passed on to the partitioning function as p_batch_interval argument. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given.")
parser.add_argument('-b','--batch', default=0, type=int, help="""How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and make at most -b partition(s). Script commits at the end of each individual batch. (NOT passed as p_batch_count to partitioning function). If not set, all data in the parent table will be partitioned in a single run of the script.""")
parser.add_argument('-w','--wait', default=0, type=float, help="Cause the script to pause for a given number of seconds between commits (batches) to reduce write load")
parser.add_argument('-o', '--order', choices=["ASC", "DESC"], default="ASC", help="Allows you to specify the order that data is migrated from the parent to the children, either ascending (ASC) or descending (DESC). Default is ASC.")
parser.add_argument('-l','--lockwait', default=0, type=float, help="Have a lock timeout of this many seconds on the data move. If a lock is not obtained, that batch will be tried again.")
parser.add_argument('--lockwait_tries', default=10, type=int, help="Number of times to allow a lockwait to time out before giving up on the partitioning.  Defaults to 10")
parser.add_argument('--autovacuum_on', action="store_true", help="Turning autovacuum off requires a brief lock to ALTER the table property. Set this option to leave autovacuum on and avoid the lock attempt.")
parser.add_argument('-q','--quiet', action="store_true", help="Switch setting to stop all output during and after partitioning for use in cron jobs")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
parser.add_argument('--debug', action="store_true", help="Show additional debugging output")
args = parser.parse_args()

is_autovac_off = False

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
    partman_schema = "\"" + cur.fetchone()[0] + "\""
    cur.close()
    return partman_schema


def get_quoted_parent_table(conn):
    cur = conn.cursor()
    sql = "SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s"
    cur.execute(sql, [args.parent])
    result = cur.fetchone()
    if result == None:
        print("Given parent table ("+args.parent+") does not exist")
        sys.exit(2)
    quoted_parent_table = "\"" + result[0] + "\".\"" + result[1] + "\""
    cur.close()
    return quoted_parent_table

def partition_data(conn, partman_schema):
    batch_count = 0
    total = 0
    lockwait_count = 0

    cur = conn.cursor()

    sql = "SELECT " + partman_schema + ".partition_data_" + args.type + "(%s"
    if args.interval != "":
        sql += ", p_batch_interval := %s"
    sql += ", p_lock_wait := %s"
    sql += ", p_order := %s"
    sql += ", p_analyze := FALSE)"

    while True:
        if args.interval != "":
            li = [args.parent, args.interval, args.lockwait, args.order]
        else:
            li = [args.parent, args.lockwait, args.order]
        if args.debug:
            print(cur.mogrify(sql, li))
        cur.execute(sql, li)
        result = cur.fetchone()
        if not args.quiet:
            if result[0] > 0:
                print("Rows moved: " + str(result[0]))
            elif result[0] == -1:
                print("Unable to obtain lock, trying again")
                print(conn.notices[-1])
        # if lock wait timeout, do not increment the counter
        if result[0] != -1:
            batch_count += 1
            total += result[0]
            lockwait_count = 0
        else:
            lockwait_count += 1
            if lockwait_count > args.lockwait_tries:
                print("Quitting due to inability to get lock on next rows to be moved")
                break
        # If no rows left or given batch argument limit is reached
        if (result[0] == 0) or (args.batch > 0 and batch_count >= int(args.batch)):
            break
        time.sleep(args.wait)

    return total


def print_version():
    print(partman_version)
    sys.exit()


def reset_autovacuum(partman_schema, quoted_parent_table):
    global is_autovac_off
    vacuum_conn = create_conn()
    cur = vacuum_conn.cursor()
    sql = "ALTER TABLE " + quoted_parent_table + " RESET (autovacuum_enabled, toast.autovacuum_enabled)"
    if not args.quiet:
        print("Attempting to reset autovacuum for old parent table and all child tables...")
    if args.debug:
        print(cur.mogrify(sql))
    cur.execute(sql)
    sql = "SELECT partition_schemaname, partition_tablename FROM " + partman_schema + ".show_partitions(%s)"
    if args.debug:
        print(cur.mogrify(sql, [args.parent]))
    cur.execute(sql, [args.parent])
    result = cur.fetchall()
    for r in result:
        sql = "ALTER TABLE \"" + r[0] + "\".\"" + r[1] + "\" RESET (autovacuum_enabled, toast.autovacuum_enabled)"
        if args.debug:
            print(cur.mogrify(sql))
        cur.execute(sql)
    print("\t... Success!")
    is_autovac_off = False
    cur.close()
    close_conn(vacuum_conn)


def signal_handler(signum, frame):
    if is_autovac_off == True:
        reset_autovacuum(partman_schema, quoted_parent_table)
    sys.exit(2)


def turn_off_autovacuum(partman_schema, quoted_parent_table):
    global is_autovac_off
    vacuum_conn = create_conn()
    cur = vacuum_conn.cursor()
    sql = "ALTER TABLE " + quoted_parent_table + " SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)"
    if not args.quiet:
        print("Attempting to turn off autovacuum for partition set...")
    if args.debug:
        print(cur.mogrify(sql))
    cur.execute(sql)
    sql = "SELECT partition_schemaname, partition_tablename FROM " + partman_schema + ".show_partitions(%s)"
    if args.debug:
        print(cur.mogrify(sql, [args.parent]))
    cur.execute(sql, [args.parent])
    result = cur.fetchall()
    for r in result:
        sql = "ALTER TABLE \"" + r[0] + "\".\"" + r[1] + "\" SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)"
        if args.debug:
            print(cur.mogrify(sql))
        cur.execute(sql)
    print("\t... Success!")
    is_autovac_off = True
    cur.close()
    close_conn(vacuum_conn)


def vacuum_parent(conn, quoted_parent_table):
    cur = conn.cursor()
    sql = "VACUUM ANALYZE " + quoted_parent_table
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

    if args.type == None:
        print("-t/--type option is required")
        sys.exit(2)

    is_autovac_off = False
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    conn = create_conn()
    partman_schema = get_partman_schema(conn)
    quoted_parent_table = get_quoted_parent_table(conn)

    try:
        if not args.autovacuum_on:
            turn_off_autovacuum(partman_schema, quoted_parent_table)

        total = partition_data(conn, partman_schema)

        if not args.quiet:
            print("Total rows moved: %d" % total)

        vacuum_parent(conn, quoted_parent_table)

    except:
        if not args.autovacuum_on and is_autovac_off == True:
            reset_autovacuum(partman_schema, quoted_parent_table)
        sys.exit(2)

    if not args.autovacuum_on:
        reset_autovacuum(partman_schema, quoted_parent_table)

    close_conn(conn)
