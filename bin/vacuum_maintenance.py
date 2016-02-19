#!/usr/bin/env python

import argparse, psycopg2, sys

partman_version = "2.3.0"

parser = argparse.ArgumentParser(description="Script for performing additional vacuum maintenance on the child tables of a partition set managed by pg_partman in order to avoid excess vacuuming and transaction id wraparound issues. Whether a table is vacuumed or not by this script depends on whether its age(relfrozenxid) is greater than vacuum_freeze_min_age. This ensures repeated runs of this script do not re-vacuum tables that have already had all of their page tuples frozen. This script does not require pg_partman to be installed and will work on any inheritance set given a parent table.")
parser.add_argument('-p', '--parent', help="Parent table of an already created partition set. (Required)")
parser.add_argument('-c', '--connection', default="host=", help="""Connection string for use by psycopg. Defaults to "host=" (local socket).""")
parser.add_argument('-z', '--freeze', action="store_true", help="Sets the FREEZE option to the VACUUM command.")
parser.add_argument('-f', '--full', action="store_true", help="Sets the FULL option to the VACUUM command. Note that --freeze is not necessary if you set this. Recommend reviewing --dryrun before running this since it will lock all tables it runs against, possibly including the parent.")
parser.add_argument('-a', '--vacuum_freeze_min_age', type=int, help="By default the script obtains this value from the system catalogs. By setting this, you can override the value obtained from the database. Note this does not change the value in the database, only the value this script uses.")
parser.add_argument('--noparent', action="store_true", help="Normally the parent table is included in the list of tables to vacuum if its age(relfrozenxid) is higher than vacuum_freeze_min_age. Set this to force exclusion of the parent table, even if it meets that criteria.")
parser.add_argument('--dryrun', action="store_true", help="Show what the script will do without actually running it against the database. Highly recommend reviewing this before running for the first time.")
parser.add_argument('-q', '--quiet', action="store_true", help="Turn off all output.")
parser.add_argument('--debug', action="store_true", help="Show additional debugging output")
args = parser.parse_args()


def close_conn(conn):
    conn.close()


def create_conn():
    conn = psycopg2.connect(args.connection)
    conn.autocommit = True
    return conn


def get_partition_list(conn):
    cur = conn.cursor()
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
        JOIN parent_info pi ON h.inhparent = pi.oid"""
    if args.noparent == False:
        sql += """
        UNION
        SELECT schemaname, tablename FROM pg_catalog.pg_tables WHERE schemaname||'.'||tablename = %s
        ORDER BY 1,2"""

    if args.debug:
        if args.noparent == False:
            print(cur.mogrify(sql, [args.parent, args.parent]))
        else:
            print(cur.mogrify(sql, [args.parent]))

    if args.noparent == False:
        cur.execute(sql, [args.parent, args.parent])
    else:
        cur.execute(sql, [args.parent])

    child_list = cur.fetchall()
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


def vacuum_table(conn, schemaname, tablename, vacuum_freeze_min_age):
    cur = conn.cursor()
    sql = """SELECT age(relfrozenxid) FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON c.relnamespace = n.oid
            WHERE n.nspname = %s
            AND c.relname = %s"""
    if args.debug:
        cur.mogrify(sql, [schemaname, tablename])
    cur.execute(sql, [schemaname, tablename])
    table_age = cur.fetchone()[0]
    if args.debug:
        print("table age ("+tablename+"): " + str(table_age))
    if table_age > vacuum_freeze_min_age:
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

    if args.parent == None:
        print("-p/--parent option is required")
        sys.exit(2)

    if args.parent.find(".") < 0:
        print("Parent table must be schema qualified")
        sys.exit(2)

    main_conn = create_conn()
    partition_list = get_partition_list(main_conn)
    vacuum_freeze_min_age = get_vacuum_freeze_min_age(main_conn)
    if args.debug:
        print("vacuum_freeze_min_age: " + str(vacuum_freeze_min_age))
    for p in partition_list:
        vacuum_table(main_conn, p[0], p[1], vacuum_freeze_min_age)

    close_conn(main_conn)
