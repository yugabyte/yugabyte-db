#!/usr/bin/env python

import argparse, psycopg2, sys

partman_version = "2.3.2"

parser = argparse.ArgumentParser(description="Script for performing additional vacuum maintenance on the child tables of a partition set in order to avoid excess vacuuming and transaction id wraparound issues. Whether a table is vacuumed or not by this script depends on whether its age(relfrozenxid) is greater than vacuum_freeze_min_age. This ensures repeated runs of this script do not re-vacuum tables that have already had all of their page tuples frozen. This script does not require pg_partman to be installed and will work on any inheritance set given a parent table, as long as the options --all, --type or --interval are NOT set.")
parser.add_argument('-p', '--parent', help="Parent table of an already created partition/inheritance set. Either this option or --all is required.")
parser.add_argument('--all', action="store_true", help="Run against all tables managed by pg_partman. Either this option or -p is required. Overrides -p if both are set.")
parser.add_argument('-c', '--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('-z', '--freeze', action="store_true", help="Sets the FREEZE option to the VACUUM command.")
parser.add_argument('-f', '--full', action="store_true", help="Sets the FULL option to the VACUUM command. Note that --freeze is not necessary if you set this. Recommend reviewing --dryrun before running this since it will lock all tables it runs against, possibly including the parent.")
parser.add_argument('-a', '--vacuum_freeze_min_age', type=int, help="By default the script obtains this value from the system catalogs. By setting this, you can override the value obtained from the database. Note this does not change the value in the database, only the value this script uses.")
parser.add_argument('-t', '--type', choices=["time","id",], help="Tells the script whether the value given by --interval is either time or id based. Also, if --all is set, limits the partition sets that the script runs against to just those types. Valid values are: \"id\" and \"time\".")
parser.add_argument('-i', '--interval', help="In addition to checking transaction age, only run against tables that are older than the interval given. Takes either a postgresql time interval or integer value. For time-based partitioning, uses the time that script is run as baseline. For id-based partitioning, the integer value will set that any partitions with an id value less than the current maximum id value minus the given value will be vacuumed. For example, if the current max id is 100 and the given value is 30, any partitions with id values less than 70 will be vacuumed. The current maximum id value at the time the script is run is always used.")
parser.add_argument('--noparent', action="store_true", help="Normally the parent table is included in the list of tables to vacuum if its age(relfrozenxid) is higher than vacuum_freeze_min_age. Set this to force exclusion of the parent table, even if it meets that criteria.")
parser.add_argument('--dryrun', action="store_true", help="Show what the script will do without actually running it against the database. Highly recommend reviewing this before running for the first time.")
parser.add_argument('-q', '--quiet', action="store_true", help="Turn off all output.")
parser.add_argument('--version', action="store_true", help="Print out the minimum version of pg_partman this script is meant to work with. The version of pg_partman installed may be greater than this.")
parser.add_argument('--debug', action="store_true", help="Show additional debugging output")
args = parser.parse_args()


def close_conn(conn):
    conn.close()


def create_conn():
    conn = psycopg2.connect(args.connection)
    conn.autocommit = True
    return conn


def get_parent_table_list(conn):
    cur = conn.cursor()
    partman_schema = get_partman_schema(conn)
    sql = "SELECT parent_table FROM " + partman_schema + ".part_config"
    if args.type == "id":
        sql += " WHERE partition_type = 'id' "
    elif args.type == "time":
        sql += " WHERE partition_type = 'time' OR partition_type = 'time-custom' "
    sql += " ORDER BY parent_table"
    cur.execute(sql)
    parent_table_list = cur.fetchall()
    cur.close()
    return parent_table_list

def get_partition_list(conn, parent_table, vacuum_freeze_min_age):
    cur = conn.cursor()
    new_child_list = []
    # Put the parent table in the list so it gets frozen as well if necessary
    # Do not use show_partitions() so this can work on non-pg_partman tables
    sql = """
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
        print(cur.mogrify(sql, [parent_table]))
    cur.execute(sql, [parent_table])
    child_list = cur.fetchall()

    # only keep tables in list with age older than vacuum_freeze_min_age
    # this compare is much faster than --interval compare below, so reduce list size earlier
    for c in child_list:
        sql = """SELECT age(relfrozenxid) FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
                WHERE n.nspname = %s
                AND c.relname = %s"""
        if args.debug:
            print(cur.mogrify(sql, [c[0], c[1]]))
        cur.execute(sql, [c[0], c[1]])
        table_age = cur.fetchone()[0]
        if args.debug:
            print("table age ("+c[1]+"): " + str(table_age))
        if table_age > vacuum_freeze_min_age:
            new_child_list.append(c)

    child_list = new_child_list

    # This may be used below with --interval as well, so just run it no matter what. Use fetchall() so tuple list is returned to match above list.
    sql = "SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s"
    if args.debug:
        print(cur.mogrify(sql, [parent_table]))
    cur.execute(sql, [parent_table])
    parent_table_split = cur.fetchall()

    if args.interval != None:
        partman_schema = get_partman_schema(conn)
        new_child_list = []  #reset to reuse variable

        if args.type == "id":
            sql = "SELECT control FROM " + partman_schema + ".part_config WHERE parent_table = %s"
            cur.execute(sql, [parent_table])
            control_col = str(cur.fetchone()[0])
            sql = "SELECT max(\"" + control_col + "\") FROM \"" + str(parent_table_split[0][0]) + "\".\"" + str(parent_table_split[0][1]) + "\""
            cur.execute(sql)
            max_id = cur.fetchone()[0]
            upper_boundary = max_id - int(args.interval)
        elif args.type == "time":
            sql = "SELECT CURRENT_TIMESTAMP - %s::interval"  # Use CURRENT_TIMESTAMP since all variables in pg_partman are timestamptz
            cur.execute(sql, [args.interval])
            upper_boundary = cur.fetchone()[0]

        else:
            print("Invalid --type value passed into get_partition_list() function.")
            sys.exit(2)

        for c in child_list:

            if args.type == "id":
                sql = "SELECT child_start_id, child_end_id FROM " + partman_schema + ".show_partition_info(%s)"
                if args.debug:
                    print(cur.mogrify(sql, [c[0] + "." + c[1]]))
                cur.execute(sql, [c[0] + "." + c[1]])
                result = cur.fetchone()
                partition_min = result[0]
                partition_max = result[1]
                if args.debug:
                    print("upper_boundary: " + str(upper_boundary) + ", partition_min: " + str(partition_min) + " partition_max: " + str(partition_max))

                if partition_min < upper_boundary and partition_max < upper_boundary:
                    new_child_list.append(c)

            elif args.type == "time":
                sql = "SELECT child_start_time, child_end_time FROM " + partman_schema + ".show_partition_info(%s)"
                if args.debug:
                    print(cur.mogrify(sql, [c[0] + "." + c[1]]))
                cur.execute(sql, [c[0] + "." + c[1]])
                result = cur.fetchone()
                partition_min = result[0]
                partition_max = result[1]
                if args.debug:
                    print("upper_boundary: " + str(upper_boundary) + ", partition_min: " + str(partition_min) + " partition_max: " + str(partition_max))

                if partition_min < upper_boundary and partition_max < upper_boundary:
                    new_child_list.append(c)

        child_list = new_child_list

    if args.noparent == False:
        child_list += parent_table_split  # add parent table to existing list

    cur.close()
    return child_list


def get_partman_schema(conn):
    cur = conn.cursor()
    sql = "SELECT nspname FROM pg_catalog.pg_namespace n, pg_catalog.pg_extension e WHERE e.extname = 'pg_partman' AND e.extnamespace = n.oid"
    cur.execute(sql)
    partman_schema = "\"" + cur.fetchone()[0] + "\""
    cur.close()
    return partman_schema


def get_vacuum_freeze_min_age(conn):
    cur = conn.cursor()
    if args.vacuum_freeze_min_age == None:
        sql = "SELECT setting FROM pg_settings WHERE name = 'vacuum_freeze_min_age'"
        cur.execute(sql)
        vacuum_freeze_min_age = cur.fetchone()[0]
    else:
        vacuum_freeze_min_age = args.vacuum_freeze_min_age
    return int(vacuum_freeze_min_age)


def print_version():
    print(partman_version)
    sys.exit()


def vacuum_table(conn, schemaname, tablename):
    cur = conn.cursor()
    sql = "VACUUM "
    if args.full:
        sql += " FULL "
    if args.freeze:
        sql += " FREEZE "
    sql += " \"" + schemaname + "\".\"" +  tablename + "\" "
    if not args.quiet:
        print(sql)
    if not args.dryrun:
        cur.execute(sql)
    cur.close()


if __name__ == "__main__":
    if args.version:
        print_version()

    if args.parent == None and args.all == False:
        print("Either the -p/--parent option or --all must be set.")
        sys.exit(2)

    if args.parent != None and args.parent.find(".") < 0:
        print("Parent table must be schema qualified")
        sys.exit(2)

    if args.interval != None and args.type == None:
        print("--interval argment requires setting --type argument as well")
        sys.exit(2)

    main_conn = create_conn()
    vacuum_freeze_min_age = get_vacuum_freeze_min_age(main_conn)
    if args.debug:
        print("vacuum_freeze_min_age: " + str(vacuum_freeze_min_age))

    if args.all:
        parent_table_list = get_parent_table_list(main_conn)
    else:
        # put in nested list because above returns as one
        parent_table_list = [[args.parent]]

    for l in parent_table_list:
        partition_list = get_partition_list(main_conn, l[0], vacuum_freeze_min_age)
        for p in partition_list:
            vacuum_table(main_conn, p[0], p[1])

    close_conn(main_conn)
