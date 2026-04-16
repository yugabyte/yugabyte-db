#!/usr/bin/env python

import argparse, psycopg2, signal, sys, time

partman_version = "4.0.0"

parser = argparse.ArgumentParser(description="This script calls either undo_partition(), undo_partition_time(), undo_partition_id(), or undo_partition_native() depending on the value given for --type. A commit is done at the end of each --interval and/or emptied partition. Returns the total number partitions and rows undone. Automatically stops when last child table is empty.", epilog="NOTE: To help avoid heavy load and contention during the undo, autovacuum is turned off for the entire partition set and all child tables when this script is run. When the undo is complete, autovacuum is set back to its default for the parent. Any child tables left behind will still have it turned off.")
parser.add_argument('-p','--parent', help="Parent table of the partition set. (Required)")
parser.add_argument('-t','--type', choices=["partman","native",], help="""Type of partitioning. Valid values are "partman" for old, trigger-based partition sets or "native" for natively partitioned sets. """)
parser.add_argument('-c','--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket). Note that two connections are required if allowing autovacuum to be turned off.""")
parser.add_argument('-i','--interval', help="Value that is passed on to the undo partitioning function as p_batch_interval. Use this to set an interval smaller than the partition interval to commit data in smaller batches. Defaults to the partition interval if not given. If -t value is not set, interval cannot be smaller than the partition interval and an entire partition is copied each batch.")
parser.add_argument('-b','--batch', type=int, default=0, help="How many times to loop through the value given for --interval. If --interval not set, will use default partition interval and undo at most -b partition(s).  Script commits at the end of each individual batch. (NOT passed as p_batch_count to undo function). If not set, all data will be moved to the parent table in a single run of the script.")
parser.add_argument('--native_target', help="""If unpartitioning a natively partitioned table, use this option to set the target table to send the data to. This option is required if -t is set to "native".""")
parser.add_argument('-d', '--droptable', action="store_true", help="Switch setting for whether to drop child tables when they are empty. Do not set to just uninherit.")
parser.add_argument('-w','--wait', type=float, default=0, help="Cause the script to pause for a given number of seconds between commits (batches).")
parser.add_argument('-l','--lockwait', default=0, type=float, help="Have a lock timeout of this many seconds on the data move. If a lock is not obtained, that batch will be tried again.")
parser.add_argument('--lockwait_tries', default=10, type=int, help="Number of times to allow a lockwait to time out before giving up on the partitioning.  Defaults to 10")
parser.add_argument('--autovacuum_on', action="store_true", help="Turning autovacuum off requires a brief lock to ALTER the table property. Set this option to leave autovacuum on and avoid the lock attempt.")
parser.add_argument('-q', '--quiet', action="store_true", help="Switch setting to stop all output during and after partitioning undo.")
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
    query = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(query)
    partman_schema = "\"" + cur.fetchone()[0] + "\""
    cur.close()
    return partman_schema


def get_quoted_parent_table(conn):
    cur = conn.cursor()
    query = "SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s"
    cur.execute(query, [args.parent])
    result = cur.fetchone()
    if result == None:
        print("Given parent table ("+args.parent+") does not exist")
        sys.exit(2)
    quoted_parent_table = "\"" + result[0] + "\".\"" + result[1] + "\""
    cur.close()
    return quoted_parent_table


def print_version():
    print(partman_version)
    sys.exit()


def reset_autovacuum(partman_schema, quoted_parent_table):
    global is_autovac_off
    vacuum_conn = create_conn()
    cur = vacuum_conn.cursor()
    # Parameter doesn't exist on native parent tables
    if args.type == "partman":
        query = "ALTER TABLE " + quoted_parent_table + " RESET (autovacuum_enabled, toast.autovacuum_enabled)"
        if not args.quiet:
            print("Attempting to reset autovacuum for old parent table...")
        if args.debug:
            print(cur.mogrify(query))
        cur.execute(query)

    # Do not use show_partitions() so that this can work on non-pg_partman partition sets
    # query = "SELECT partition_schemaname, partition_tablename FROM " + partman_schema + ".show_partitions(%s)"
    query = """
        WITH parent_info AS (
            SELECT c1.oid FROM pg_catalog.pg_class c1
            JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
            WHERE n1.nspname ||'.'|| c1.relname = %s
        )
        SELECT n.nspname::text, c.relname::text AS partition_name FROM
        pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        JOIN parent_info pi ON h.inhparent = pi.oid
        ORDER BY 1,2"""

    if args.debug:
        print(cur.mogrify(query, [args.parent]))
    cur.execute(query, [args.parent])
    result = cur.fetchall()
    for r in result:
        query = "ALTER TABLE \"" + r[0] + "\".\"" + r[1] + "\" RESET (autovacuum_enabled, toast.autovacuum_enabled)"
        if args.debug:
            print(cur.mogrify(query))
        cur.execute(query)
    print("\t... Success!")
    is_autovac_off = False
    cur.close()
    close_conn(vacuum_conn)


def signal_handler(signum, frame):
    if is_autovac_off == True:
        reset_autovacuum(conn, partman_schema, quoted_parent_table)
    sys.exit(2)

def turn_off_autovacuum(partman_schema, quoted_parent_table):
    global is_autovac_off
    vacuum_conn = create_conn()
    cur = vacuum_conn.cursor()
    # Parameter doesn't exist on native parent tables
    if args.type == "partman":
        query = "ALTER TABLE " + quoted_parent_table + " SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)"
        if not args.quiet:
            print("Attempting to turn off autovacuum for partition set...")
        if args.debug:
            print(cur.mogrify(query))
        cur.execute(query)

    # Do not use show_partitions() so that this can work on non-pg_partman partition sets
    # query = "SELECT partition_schemaname, partition_tablename FROM " + partman_schema + ".show_partitions(%s)"
    query = """
        WITH parent_info AS (
            SELECT c1.oid FROM pg_catalog.pg_class c1
            JOIN pg_catalog.pg_namespace n1 ON c1.relnamespace = n1.oid
            WHERE n1.nspname ||'.'|| c1.relname = %s
        )
        SELECT n.nspname::text, c.relname::text AS partition_name FROM
        pg_catalog.pg_inherits h
        JOIN pg_catalog.pg_class c ON c.oid = h.inhrelid
        JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
        JOIN parent_info pi ON h.inhparent = pi.oid
        ORDER BY 1,2"""
    if args.debug:
        print(cur.mogrify(query, [args.parent]))
    cur.execute(query, [args.parent])
    result = cur.fetchall()
    for r in result:
        query = "ALTER TABLE \"" + r[0] + "\".\"" + r[1] + "\" SET (autovacuum_enabled = false, toast.autovacuum_enabled = false)"
        if args.debug:
            print(cur.mogrify(query))
        cur.execute(query)
    print("\t... Success!")
    is_autovac_off = True
    cur.close()
    close_conn(vacuum_conn)


def undo_partition_data(conn, partman_schema):
    batch_count = 0
    total_partitions = 0
    total_rows = 0
    lockwait_count = 0

    cur = conn.cursor()

    # Actual undo sql functions do not drop by default, so fix argument value to match that default
    if args.droptable:
        keep_table = False
    else:
        keep_table = True

    query = "SELECT partitions_undone, rows_undone FROM " + partman_schema + ".undo_partition"

    query += "(p_parent_table := %s, p_keep_table := %s, p_lock_wait := %s "
    if args.interval != None:
        query += ", p_batch_interval := %s "
    if args.type == "native":
        if args.native_target == None:
            print("""--native_target must be set if --type is set to "native" """)
            sys.exit(2)
        query += ", p_target_table := %s "
    query += ")"

    while True:
        #if args.interval != None:
        #    li = [args.parent, keep_table, args.interval, args.lockwait]
        #else:
        #    li = [args.parent, keep_table, args.lockwait]
        li = [args.parent, keep_table, args.lockwait]
        if args.interval != None:
            li.append(args.interval)
        if args.type == "native":
            li.append(args.native_target)
        if args.debug:
            print(cur.mogrify(query, li))
        cur.execute(query, li)

        result = cur.fetchone()
        if args.debug:
            print("Undo result: " + str(result))

        conn.commit()
        if not args.quiet:
            if result[0] > 0 or result[1] > 0:
                print("Partitions undone this batch: " + str(result[0]) + ". Rows undone this batch: " + str(result[1]))
            elif result[0] == -1:
                print("Unable to obtain lock, trying again (" + str(lockwait_count+1) + ")")
                print(conn.notices[-1])
        # if lock wait timeout, do not increment the counter
        if result[0] != -1:
            batch_count += 1
            total_partitions += result[0]
            total_rows += result[1]
            lockwait_count = 0
        else:
            lockwait_count += 1
            if lockwait_count > args.lockwait_tries:
                print("Quitting due to inability to get lock on table/rows for migration to parent")
                break
        print("Total rows moved: " + str(total_rows))
        # If given batch argument limit is reached
        if (args.batch > 0 and batch_count >= int(args.batch)):
            break
        # undo_partition functions will remove config entry once last child is dropped
        config_query = "SELECT parent_table FROM " + partman_schema + ".part_config WHERE parent_table = %s"
        if args.debug:
            print(cur.mogrify(config_query, [args.parent]))
        cur.execute(config_query, [args.parent])
        result = cur.fetchone()
        if result == None:
            break
        if args.wait > 0:
            time.sleep(args.wait)

    return total_partitions

def vacuum_parent(conn, quoted_parent_table):
    cur = conn.cursor()
    query = "VACUUM ANALYZE " + quoted_parent_table
    if args.debug:
        print(cur.mogrify(query))
    if not args.quiet:
        print("Running vacuum analyze on parent table...")
    cur.execute(query)
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

    if args.type == "native" and args.native_target == None:
            print("""--native_target must be set if --type is set to "native" """)
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

        total = undo_partition_data(conn, partman_schema)

        if not args.quiet:
            print("Total partitions undone: %d" % total)

        vacuum_parent(conn, quoted_parent_table)

    except:
        if not args.autovacuum_on and is_autovac_off == True:
            reset_autovacuum(partman_schema, quoted_parent_table)
        raise

    if not args.autovacuum_on:
        reset_autovacuum(partman_schema, quoted_parent_table)

    close_conn(conn)
